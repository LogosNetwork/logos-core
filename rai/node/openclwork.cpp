#include <rai/node/openclwork.hpp>

#include <rai/lib/utility.hpp>
#include <rai/node/node.hpp>
#include <rai/node/wallet.hpp>

#include <array>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
std::string opencl_program = R"%%%(
enum blake2b_constant
{
	BLAKE2B_BLOCKBYTES = 128,
	BLAKE2B_OUTBYTES   = 64,
	BLAKE2B_KEYBYTES   = 64,
	BLAKE2B_SALTBYTES  = 16,
	BLAKE2B_PERSONALBYTES = 16
};

typedef struct __blake2b_param
{
	uchar  digest_length; // 1
	uchar  key_length;    // 2
	uchar  fanout;        // 3
	uchar  depth;         // 4
	uint leaf_length;   // 8
	ulong node_offset;   // 16
	uchar  node_depth;    // 17
	uchar  inner_length;  // 18
	uchar  reserved[14];  // 32
	uchar  salt[BLAKE2B_SALTBYTES]; // 48
	uchar  personal[BLAKE2B_PERSONALBYTES];  // 64
} blake2b_param;

typedef struct __blake2b_state
{
	ulong h[8];
	ulong t[2];
	ulong f[2];
	uchar  buf[2 * BLAKE2B_BLOCKBYTES];
	size_t   buflen;
	uchar  last_node;
} blake2b_state;

__constant static ulong blake2b_IV[8] =
{
	0x6a09e667f3bcc908UL, 0xbb67ae8584caa73bUL,
	0x3c6ef372fe94f82bUL, 0xa54ff53a5f1d36f1UL,
	0x510e527fade682d1UL, 0x9b05688c2b3e6c1fUL,
	0x1f83d9abfb41bd6bUL, 0x5be0cd19137e2179UL
};

__constant static uchar blake2b_sigma[12][16] =
{
  {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
  { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
  { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
  {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
  {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
  {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
  { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
  { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
  {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
  { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
  {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
  { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }
};


static inline int blake2b_set_lastnode( blake2b_state *S )
{
  S->f[1] = ~0UL;
  return 0;
}

/* Some helper functions, not necessarily useful */
static inline int blake2b_set_lastblock( blake2b_state *S )
{
  if( S->last_node ) blake2b_set_lastnode( S );

  S->f[0] = ~0UL;
  return 0;
}

static inline int blake2b_increment_counter( blake2b_state *S, const ulong inc )
{
  S->t[0] += inc;
  S->t[1] += ( S->t[0] < inc );
  return 0;
}

static inline uint load32( const void *src )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  return *( uint * )( src );
#else
  const uchar *p = ( uchar * )src;
  uint w = *p++;
  w |= ( uint )( *p++ ) <<  8;
  w |= ( uint )( *p++ ) << 16;
  w |= ( uint )( *p++ ) << 24;
  return w;
#endif
}

static inline ulong load64( const void *src )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  return *( ulong * )( src );
#else
  const uchar *p = ( uchar * )src;
  ulong w = *p++;
  w |= ( ulong )( *p++ ) <<  8;
  w |= ( ulong )( *p++ ) << 16;
  w |= ( ulong )( *p++ ) << 24;
  w |= ( ulong )( *p++ ) << 32;
  w |= ( ulong )( *p++ ) << 40;
  w |= ( ulong )( *p++ ) << 48;
  w |= ( ulong )( *p++ ) << 56;
  return w;
#endif
}

static inline void store32( void *dst, uint w )
{
#if defined(__ENDIAN_LITTLE__)
  *( uint * )( dst ) = w;
#else
  uchar *p = ( uchar * )dst;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w;
#endif
}

static inline void store64( void *dst, ulong w )
{
#if defined(__ENDIAN_LITTLE__)
  *( ulong * )( dst ) = w;
#else
  uchar *p = ( uchar * )dst;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w; w >>= 8;
  *p++ = ( uchar )w;
#endif
}

static inline ulong rotr64( const ulong w, const unsigned c )
{
  return ( w >> c ) | ( w << ( 64 - c ) );
}

static void ucharset (void * dest_a, int val, size_t count)
{
	uchar * dest = (uchar *)dest_a;
	for (size_t i = 0; i < count; ++i)
	{
		*dest++ = val;
	}
}

/* init xors IV with input parameter block */
static inline int blake2b_init_param( blake2b_state *S, const blake2b_param *P )
{
  uchar *p, *h;
  __constant uchar *v;
  v = ( __constant uchar * )( blake2b_IV );
  h = ( uchar * )( S->h );
  p = ( uchar * )( P );
  /* IV XOR ParamBlock */
  ucharset( S, 0, sizeof( blake2b_state ) );

  for( int i = 0; i < BLAKE2B_OUTBYTES; ++i ) h[i] = v[i] ^ p[i];

  return 0;
}

static inline int blake2b_init( blake2b_state *S, const uchar outlen )
{
  blake2b_param P[1];

  if ( ( !outlen ) || ( outlen > BLAKE2B_OUTBYTES ) ) return -1;

  P->digest_length = outlen;
  P->key_length    = 0;
  P->fanout        = 1;
  P->depth         = 1;
  store32( &P->leaf_length, 0 );
  store64( &P->node_offset, 0 );
  P->node_depth    = 0;
  P->inner_length  = 0;
  ucharset( P->reserved, 0, sizeof( P->reserved ) );
  ucharset( P->salt,     0, sizeof( P->salt ) );
  ucharset( P->personal, 0, sizeof( P->personal ) );
  return blake2b_init_param( S, P );
}

static int blake2b_compress( blake2b_state *S, __private const uchar block[BLAKE2B_BLOCKBYTES] )
{
  ulong m[16];
  ulong v[16];
  int i;

  for( i = 0; i < 16; ++i )
	m[i] = load64( block + i * sizeof( m[i] ) );

  for( i = 0; i < 8; ++i )
	v[i] = S->h[i];

  v[ 8] = blake2b_IV[0];
  v[ 9] = blake2b_IV[1];
  v[10] = blake2b_IV[2];
  v[11] = blake2b_IV[3];
  v[12] = S->t[0] ^ blake2b_IV[4];
  v[13] = S->t[1] ^ blake2b_IV[5];
  v[14] = S->f[0] ^ blake2b_IV[6];
  v[15] = S->f[1] ^ blake2b_IV[7];
#define G(r,i,a,b,c,d) \
  do { \
	a = a + b + m[blake2b_sigma[r][2*i+0]]; \
	d = rotr64(d ^ a, 32); \
	c = c + d; \
	b = rotr64(b ^ c, 24); \
	a = a + b + m[blake2b_sigma[r][2*i+1]]; \
	d = rotr64(d ^ a, 16); \
	c = c + d; \
	b = rotr64(b ^ c, 63); \
  } while(0)
#define ROUND(r)  \
  do { \
	G(r,0,v[ 0],v[ 4],v[ 8],v[12]); \
	G(r,1,v[ 1],v[ 5],v[ 9],v[13]); \
	G(r,2,v[ 2],v[ 6],v[10],v[14]); \
	G(r,3,v[ 3],v[ 7],v[11],v[15]); \
	G(r,4,v[ 0],v[ 5],v[10],v[15]); \
	G(r,5,v[ 1],v[ 6],v[11],v[12]); \
	G(r,6,v[ 2],v[ 7],v[ 8],v[13]); \
	G(r,7,v[ 3],v[ 4],v[ 9],v[14]); \
  } while(0)
  ROUND( 0 );
  ROUND( 1 );
  ROUND( 2 );
  ROUND( 3 );
  ROUND( 4 );
  ROUND( 5 );
  ROUND( 6 );
  ROUND( 7 );
  ROUND( 8 );
  ROUND( 9 );
  ROUND( 10 );
  ROUND( 11 );

  for( i = 0; i < 8; ++i )
	S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];

#undef G
#undef ROUND
  return 0;
}

static void ucharcpy (uchar * dst, uchar const * src, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		*dst++ = *src++;
	}
}

void printstate (blake2b_state * S)
{
	printf ("%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ", S->h[0], S->h[1], S->h[2], S->h[3], S->h[4], S->h[5], S->h[6], S->h[7], S->t[0], S->t[1], S->f[0], S->f[1]);
	for (int i = 0; i < 256; ++i)
	{
		printf ("%02x", S->buf[i]);
	}
	printf (" %lu %02x\n", S->buflen, S->last_node);
}

/* inlen now in bytes */
static int blake2b_update( blake2b_state *S, const uchar *in, ulong inlen )
{
  while( inlen > 0 )
  {
	size_t left = S->buflen;
	size_t fill = 2 * BLAKE2B_BLOCKBYTES - left;

	if( inlen > fill )
	{
	  ucharcpy( S->buf + left, in, fill ); // Fill buffer
	  S->buflen += fill;
	  blake2b_increment_counter( S, BLAKE2B_BLOCKBYTES );
	  blake2b_compress( S, S->buf ); // Compress
	  ucharcpy( S->buf, S->buf + BLAKE2B_BLOCKBYTES, BLAKE2B_BLOCKBYTES ); // Shift buffer left
	  S->buflen -= BLAKE2B_BLOCKBYTES;
	  in += fill;
	  inlen -= fill;
	}
	else // inlen <= fill
	{
	  ucharcpy( S->buf + left, in, inlen );
	  S->buflen += inlen; // Be lazy, do not compress
	  in += inlen;
	  inlen -= inlen;
	}
  }

  return 0;
}

/* Is this correct? */
static int blake2b_final( blake2b_state *S, uchar *out, uchar outlen )
{
  uchar buffer[BLAKE2B_OUTBYTES];

  if( S->buflen > BLAKE2B_BLOCKBYTES )
  {
	blake2b_increment_counter( S, BLAKE2B_BLOCKBYTES );
	blake2b_compress( S, S->buf );
	S->buflen -= BLAKE2B_BLOCKBYTES;
	ucharcpy( S->buf, S->buf + BLAKE2B_BLOCKBYTES, S->buflen );
  }

  //blake2b_increment_counter( S, S->buflen );
  ulong inc = (ulong)S->buflen;
  S->t[0] += inc;
//  if ( S->t[0] < inc )
//    S->t[1] += 1;
  // This seems to crash the opencl compiler though fortunately this is calculating size and we don't do things bigger than 2^32
	
  blake2b_set_lastblock( S );
  ucharset( S->buf + S->buflen, 0, 2 * BLAKE2B_BLOCKBYTES - S->buflen ); /* Padding */
  blake2b_compress( S, S->buf );

  for( int i = 0; i < 8; ++i ) /* Output full hash to temp buffer */
	store64( buffer + sizeof( S->h[i] ) * i, S->h[i] );

  ucharcpy( out, buffer, outlen );
  return 0;
}

static void ucharcpyglb (uchar * dst, __global uchar const * src, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		*dst = *src;
		++dst;
		++src;
	}
}
	
__kernel void raiblocks_work (__global ulong * attempt, __global ulong * result_a, __global uchar * item_a)
{
	int const thread = get_global_id (0);
	uchar item_l [32];
	ucharcpyglb (item_l, item_a, 32);
	ulong attempt_l = *attempt + thread;
	blake2b_state state;
	blake2b_init (&state, sizeof (ulong));
	blake2b_update (&state, (uchar *) &attempt_l, sizeof (ulong));
	blake2b_update (&state, item_l, 32);
	ulong result;
	blake2b_final (&state, (uchar *) &result, sizeof (result));
	if (result >= 0xffffffc000000000ul)
	//if (result >= 0xff00000000000000ul)
	{
		*result_a = attempt_l;
	}
}
)%%%";
}

void printstate (blake2b_state * S)
{
	printf ("%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", S->h[0], S->h[1], S->h[2], S->h[3], S->h[4], S->h[5], S->h[6], S->h[7], S->t[0], S->t[1], S->f[0], S->f[1]);
	for (int i = 0; i < 256; ++i)
	{
		printf ("%02x", S->buf[i]);
	}
	printf (" %lu %02x\n", S->buflen, S->last_node);
}

rai::opencl_environment::opencl_environment (bool & error_a)
{
	cl_uint platformIdCount = 0;
	clGetPlatformIDs (0, nullptr, &platformIdCount);
	std::vector<cl_platform_id> platformIds (platformIdCount);
	clGetPlatformIDs (platformIdCount, platformIds.data (), nullptr);
	for (auto i (platformIds.begin ()), n (platformIds.end ()); i != n; ++i)
	{
		rai::opencl_platform platform;
		platform.platform = *i;
		cl_uint deviceIdCount = 0;
		clGetDeviceIDs (*i, CL_DEVICE_TYPE_ALL, 0, nullptr, &deviceIdCount);
		std::vector<cl_device_id> deviceIds (deviceIdCount);
		clGetDeviceIDs (*i, CL_DEVICE_TYPE_ALL, deviceIdCount, deviceIds.data (), nullptr);
		for (auto j (deviceIds.begin ()), m (deviceIds.end ()); j != m; ++j)
		{
			platform.devices.push_back (*j);
		}
		platforms.push_back (platform);
	}
}

void rai::opencl_environment::dump (std::ostream & stream)
{
	auto index (0);
	auto device_count (0);
	for (auto & i : platforms)
	{
		device_count += i.devices.size ();
	}
	stream << boost::str (boost::format ("OpenCL found %1% platforms and %2% devices\n") % platforms.size () % device_count);
	for (auto i (platforms.begin ()), n (platforms.end ()); i != n; ++i, ++index)
	{
		std::vector<unsigned> queries = { CL_PLATFORM_PROFILE, CL_PLATFORM_VERSION, CL_PLATFORM_NAME, CL_PLATFORM_VENDOR, CL_PLATFORM_EXTENSIONS };
		stream << "Platform: " << index << std::endl;
		for (auto j (queries.begin ()), m (queries.end ()); j != m; ++j)
		{
			size_t platformInfoCount = 0;
			clGetPlatformInfo (i->platform, *j, 0, nullptr, &platformInfoCount);
			std::vector<char> info (platformInfoCount);
			clGetPlatformInfo (i->platform, *j, info.size (), info.data (), nullptr);
			stream << info.data () << std::endl;
		}
		for (auto j (i->devices.begin ()), m (i->devices.end ()); j != m; ++j)
		{
			std::vector<unsigned> queries = { CL_DEVICE_NAME, CL_DEVICE_VENDOR, CL_DEVICE_PROFILE };
			stream << "Device: " << j - i->devices.begin () << std::endl;
			for (auto k (queries.begin ()), o (queries.end ()); k != o; ++k)
			{
				size_t platformInfoCount = 0;
				clGetDeviceInfo (*j, *k, 0, nullptr, &platformInfoCount);
				std::vector<char> info (platformInfoCount);
				clGetDeviceInfo (*j, *k, info.size (), info.data (), nullptr);
				stream << '\t' << info.data () << std::endl;
			}
			size_t deviceTypeCount = 0;
			clGetDeviceInfo (*j, CL_DEVICE_TYPE, 0, nullptr, &deviceTypeCount);
			std::vector<uint8_t> deviceTypeInfo (deviceTypeCount);
			clGetDeviceInfo (*j, CL_DEVICE_TYPE, deviceTypeCount, deviceTypeInfo.data (), 0);
			std::string device_type_string;
			switch (deviceTypeInfo[0])
			{
				case CL_DEVICE_TYPE_ACCELERATOR:
					device_type_string = "ACCELERATOR";
					break;
				case CL_DEVICE_TYPE_CPU:
					device_type_string = "CPU";
					break;
				case CL_DEVICE_TYPE_CUSTOM:
					device_type_string = "CUSTOM";
					break;
				case CL_DEVICE_TYPE_DEFAULT:
					device_type_string = "DEFAULT";
					break;
				case CL_DEVICE_TYPE_GPU:
					device_type_string = "GPU";
					break;
				default:
					device_type_string = "Unknown";
					break;
			}
			stream << '\t' << device_type_string << std::endl;
			size_t compilerAvailableCount = 0;
			clGetDeviceInfo (*j, CL_DEVICE_COMPILER_AVAILABLE, 0, nullptr, &compilerAvailableCount);
			std::vector<uint8_t> compilerAvailableInfo (compilerAvailableCount);
			clGetDeviceInfo (*j, CL_DEVICE_COMPILER_AVAILABLE, compilerAvailableCount, compilerAvailableInfo.data (), 0);
			stream << '\t' << "Compiler available: " << (compilerAvailableInfo[0] ? "true" : "false") << std::endl;
			size_t computeUnitsAvailableCount = 0;
			clGetDeviceInfo (*j, CL_DEVICE_MAX_COMPUTE_UNITS, 0, nullptr, &computeUnitsAvailableCount);
			std::vector<uint8_t> computeUnitsAvailableInfo (computeUnitsAvailableCount);
			clGetDeviceInfo (*j, CL_DEVICE_MAX_COMPUTE_UNITS, computeUnitsAvailableCount, computeUnitsAvailableInfo.data (), 0);
			uint64_t computeUnits (computeUnitsAvailableInfo[0] | (computeUnitsAvailableInfo[1] << 8) | (computeUnitsAvailableInfo[2] << 16) | (computeUnitsAvailableInfo[3] << 24));
			stream << '\t' << "Compute units available: " << computeUnits << std::endl;
		}
	}
}

rai::opencl_config::opencl_config () :
platform (0),
device (0),
threads (1024 * 1024)
{
}

rai::opencl_config::opencl_config (unsigned platform_a, unsigned device_a, unsigned threads_a) :
platform (platform_a),
device (device_a),
threads (threads_a)
{
}

void rai::opencl_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
	tree_a.put ("platform", std::to_string (platform));
	tree_a.put ("device", std::to_string (device));
	tree_a.put ("threads", std::to_string (threads));
}

bool rai::opencl_config::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		auto platform_l (tree_a.get<std::string> ("platform"));
		auto device_l (tree_a.get<std::string> ("device"));
		auto threads_l (tree_a.get<std::string> ("threads"));
		try
		{
			platform = std::stoull (platform_l);
			device = std::stoull (device_l);
			threads = std::stoull (threads_l);
		}
		catch (std::logic_error const &)
		{
			result = true;
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

rai::opencl_work::opencl_work (bool & error_a, rai::opencl_config const & config_a, rai::opencl_environment & environment_a, rai::logging & logging_a) :
config (config_a),
context (0),
attempt_buffer (0),
result_buffer (0),
item_buffer (0),
program (0),
kernel (0),
queue (0),
logging (logging_a)
{
	error_a |= config.platform >= environment_a.platforms.size ();
	if (!error_a)
	{
		auto & platform (environment_a.platforms[config.platform]);
		error_a |= config.device >= platform.devices.size ();
		if (!error_a)
		{
			rai::random_pool.GenerateBlock (reinterpret_cast<uint8_t *> (rand.s.data ()), rand.s.size () * sizeof (decltype (rand.s)::value_type));
			std::array<cl_device_id, 1> selected_devices;
			selected_devices[0] = platform.devices[config.device];
			cl_context_properties contextProperties[] = {
				CL_CONTEXT_PLATFORM,
				reinterpret_cast<cl_context_properties> (platform.platform),
				0, 0
			};
			cl_int createContextError (0);
			context = clCreateContext (contextProperties, selected_devices.size (), selected_devices.data (), nullptr, nullptr, &createContextError);
			error_a |= createContextError != CL_SUCCESS;
			if (!error_a)
			{
				cl_int queue_error (0);
				queue = clCreateCommandQueue (context, selected_devices[0], 0, &queue_error);
				error_a |= queue_error != CL_SUCCESS;
				if (!error_a)
				{
					cl_int attempt_error (0);
					attempt_buffer = clCreateBuffer (context, 0, sizeof (uint64_t), nullptr, &attempt_error);
					error_a |= attempt_error != CL_SUCCESS;
					if (!error_a)
					{
						cl_int result_error (0);
						result_buffer = clCreateBuffer (context, 0, sizeof (uint64_t), nullptr, &result_error);
						error_a |= result_error != CL_SUCCESS;
						if (!error_a)
						{
							cl_int item_error (0);
							size_t item_size (sizeof (rai::uint256_union));
							item_buffer = clCreateBuffer (context, 0, item_size, nullptr, &item_error);
							error_a |= item_error != CL_SUCCESS;
							if (!error_a)
							{
								cl_int program_error (0);
								char const * program_data (opencl_program.data ());
								size_t program_length (opencl_program.size ());
								program = clCreateProgramWithSource (context, 1, &program_data, &program_length, &program_error);
								error_a |= program_error != CL_SUCCESS;
								if (!error_a)
								{
									auto clBuildProgramError (clBuildProgram (program, selected_devices.size (), selected_devices.data (), "-D __APPLE__", nullptr, nullptr));
									error_a |= clBuildProgramError != CL_SUCCESS;
									if (!error_a)
									{
										cl_int kernel_error (0);
										kernel = clCreateKernel (program, "raiblocks_work", &kernel_error);
										error_a |= kernel_error != CL_SUCCESS;
										if (!error_a)
										{
											cl_int arg0_error (clSetKernelArg (kernel, 0, sizeof (attempt_buffer), &attempt_buffer));
											error_a |= arg0_error != CL_SUCCESS;
											if (!error_a)
											{
												cl_int arg1_error (clSetKernelArg (kernel, 1, sizeof (result_buffer), &result_buffer));
												error_a |= arg1_error != CL_SUCCESS;
												if (!error_a)
												{
													cl_int arg2_error (clSetKernelArg (kernel, 2, sizeof (item_buffer), &item_buffer));
													error_a |= arg2_error != CL_SUCCESS;
													if (!error_a)
													{
													}
													else
													{
														BOOST_LOG (logging.log) << boost::str (boost::format ("Bind argument 2 error %1%") % arg2_error);
													}
												}
												else
												{
													BOOST_LOG (logging.log) << boost::str (boost::format ("Bind argument 1 error %1%") % arg1_error);
												}
											}
											else
											{
												BOOST_LOG (logging.log) << boost::str (boost::format ("Bind argument 0 error %1%") % arg0_error);
											}
										}
										else
										{
											BOOST_LOG (logging.log) << boost::str (boost::format ("Create kernel error %1%") % kernel_error);
										}
									}
									else
									{
										BOOST_LOG (logging.log) << boost::str (boost::format ("Build program error %1%") % clBuildProgramError);
										for (auto i (selected_devices.begin ()), n (selected_devices.end ()); i != n; ++i)
										{
											size_t log_size (0);
											clGetProgramBuildInfo (program, *i, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
											std::vector<char> log (log_size);
											clGetProgramBuildInfo (program, *i, CL_PROGRAM_BUILD_LOG, log.size (), log.data (), nullptr);
											BOOST_LOG (logging.log) << log.data ();
										}
									}
								}
								else
								{
									BOOST_LOG (logging.log) << boost::str (boost::format ("Create program error %1%") % program_error);
								}
							}
							else
							{
								BOOST_LOG (logging.log) << boost::str (boost::format ("Item buffer error %1%") % item_error);
							}
						}
						else
						{
							BOOST_LOG (logging.log) << boost::str (boost::format ("Result buffer error %1%") % result_error);
						}
					}
					else
					{
						BOOST_LOG (logging.log) << boost::str (boost::format ("Attempt buffer error %1%") % attempt_error);
					}
				}
				else
				{
					BOOST_LOG (logging.log) << boost::str (boost::format ("Unable to create command queue %1%") % queue_error);
				}
			}
			else
			{
				BOOST_LOG (logging.log) << boost::str (boost::format ("Unable to create context %1%") % createContextError);
			}
		}
		else
		{
			BOOST_LOG (logging.log) << boost::str (boost::format ("Requested device %1%, and only have %2%") % config.device % platform.devices.size ());
		}
	}
	else
	{
		BOOST_LOG (logging.log) << boost::str (boost::format ("Requested platform %1% and only have %2%") % config.platform % environment_a.platforms.size ());
	}
}

rai::opencl_work::~opencl_work ()
{
	if (kernel != 0)
	{
		clReleaseKernel (kernel);
	}
	if (program != 0)
	{
		clReleaseProgram (program);
	}
	if (context != 0)
	{
		clReleaseContext (context);
	}
}

boost::optional<uint64_t> rai::opencl_work::generate_work (rai::uint256_union const & root_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	bool error (false);
	uint64_t result (0);
	unsigned thread_count (config.threads);
	size_t work_size[] = { thread_count, 0, 0 };
	while (rai::work_validate (root_a, result) && !error)
	{
		result = rand.next ();
		cl_int write_error1 = clEnqueueWriteBuffer (queue, attempt_buffer, false, 0, sizeof (uint64_t), &result, 0, nullptr, nullptr);
		if (write_error1 == CL_SUCCESS)
		{
			cl_int write_error2 = clEnqueueWriteBuffer (queue, item_buffer, false, 0, sizeof (rai::uint256_union), root_a.bytes.data (), 0, nullptr, nullptr);
			if (write_error2 == CL_SUCCESS)
			{
				cl_int enqueue_error = clEnqueueNDRangeKernel (queue, kernel, 1, nullptr, work_size, nullptr, 0, nullptr, nullptr);
				if (enqueue_error == CL_SUCCESS)
				{
					cl_int read_error1 = clEnqueueReadBuffer (queue, result_buffer, false, 0, sizeof (uint64_t), &result, 0, nullptr, nullptr);
					if (read_error1 == CL_SUCCESS)
					{
						cl_int finishError = clFinish (queue);
						if (finishError == CL_SUCCESS)
						{
						}
						else
						{
							error = true;
							BOOST_LOG (logging.log) << boost::str (boost::format ("Error finishing queue %1%") % finishError);
						}
					}
					else
					{
						error = true;
						BOOST_LOG (logging.log) << boost::str (boost::format ("Error reading result %1%") % read_error1);
					}
				}
				else
				{
					error = true;
					BOOST_LOG (logging.log) << boost::str (boost::format ("Error enqueueing kernel %1%") % enqueue_error);
				}
			}
			else
			{
				error = true;
				BOOST_LOG (logging.log) << boost::str (boost::format ("Error writing item %1%") % write_error2);
			}
		}
		else
		{
			error = true;
			BOOST_LOG (logging.log) << boost::str (boost::format ("Error writing attempt %1%") % write_error1);
		}
	}
	boost::optional<uint64_t> value;
	if (!error)
	{
		value = result;
	}
	return value;
}

std::unique_ptr<rai::opencl_work> rai::opencl_work::create (bool create_a, rai::opencl_config const & config_a, rai::logging & logging_a)
{
	std::unique_ptr<rai::opencl_work> result;
	if (create_a)
	{
		auto error (false);
		rai::opencl_environment environment (error);
		std::stringstream stream;
		environment.dump (stream);
		BOOST_LOG (logging_a.log) << stream.str ();
		if (!error)
		{
			result.reset (new rai::opencl_work (error, config_a, environment, logging_a));
			if (error)
			{
				result.reset ();
			}
		}
	}
	return result;
}
