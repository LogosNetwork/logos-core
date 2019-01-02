/// @file
/// This file contains the declaration of the DelegateIdentityManager class, which encapsulates
/// node identity management logic. Currently it holds all delegates ip, accounts, and delegate's index (formerly id)
/// into epoch's voted delegates. It also creates genesis microblocks, epochs, and delegates genesis accounts
///
#include <logos/consensus/consensus_container.hpp>
#include <logos/microblock/microblock_handler.hpp>
#include <logos/node/delegate_identity_manager.hpp>
#include <logos/epoch/epoch_handler.hpp>
#include <logos/node/node.hpp>
#include <logos/lib/trace.hpp>


const char *bls_keys[] = {
  "0x1d413017341569eb7b7e74a031fa1a2dadaa600b449d49983fc920f9d89ae407 1 0x16d73fc6647d0f9c6c50ec2cae8a04f20e82bee1d91ad3f7e3b3db8008db64ba 0x17012477a44243795807c462a7cce92dc71d1626952cae8d78c6be6bd7c2bae4 0x13ef6f7873bc4a78feae40e9a25396a0f0a52fbb28c3d38b4bf50e18c48632c 0x7390eee94c740350098a653d57c1705b24470434709a92f624589dc8537429d",
  "0x13e9f73fe79631abd35962b3942c789bb340936053cbaca8a1cb7ff7ccdc137 1 0x10a0be05cf6518acc550c972338dac0a14a1624b2aecd73d75f6be3ae8a0f2af 0x167cf791f075c435c9d51dbb4a7afd149442233f48cf8114665822890bd6cc84 0x2398ff0823cc0aa4240023109adabfcda5c64e3ad21e3495fb63212260f3aa7b 0xc094b42a07e1adf1f8d59950ee73cc0ea48764ccf54260ed622dfc954a92aab",
  "0x1fd2b2e7be4b0d268d9412e3f21997c5def2525e6fbd9542b169c50e836a50d0 1 0x39d14c64647d6fb3fefaa306cd43404fea18a77c5d0199dcf1572c6e2c3654 0xd94dfc7b26bad250be45bfc7445efe6577541ab3c6757a0f5dfd5a95232067a 0x18d858a48b454f2c8e179e9eb31b7926341a2ecd685404083a32f03284f64b4d 0x93a1698fdf7654de985998b49922cfdf76898d891183ddd8e60347599dff3a",
  "0x23732809cc819f7f630537fb04c2dc4137d942795d79125c63dc1bd025b7b81c 1 0x52e922b7bef41c143ecf25d94df08a3c08828cdce1fe964f8df9eee37fc73c 0xae9ec67bdacb6e82844ab8ac29daa5057aaa6cb71af13eb4f350a760971b226 0x2196688cf3af72895c809fd68dee87ff2db31a467386e211a14955e95bfbea63 0x2e643fee14d9954ac8f23cc0f124d0a39cd63e204a65f372f7c3e302b57e035",
  "0xbdd5c0395c470e25626040127b2ff7a85a55c04d299784da6d84f30f8d9e02b 1 0x1693857921b018b469fca6350189fb3df902e214cd0e82518bafa40ad2ae52f4 0x1ad27969842f8b7a0aefe6abcbf5ac59891b3ca716818c94bf0f0123fe8daf02 0xa07c11b223fc087837626f5291546df0d65c577e855ed7f43430bc2b1a37a9c 0xe74351d5a0b0f61f8a23a59fc57ef8509951629be46919015264dfe035424d0",
  "0x13f7e925ea1ed0776beea77a32d99433359a734869e2453f4178938ce21c3ab6 1 0x1bedfc1afefa9d7d5ad8d7e56fe0893a28605bddd063c27e513bcd216a1bb551 0x1af9da135710b62d131c89e9f16b52671e66423b1ea486d0bb85b78f34c334d6 0x1558df8be9667bd571498067a0985e2c88e4ce185ff80a5565dc0957c73e64c 0x5d8565c98ebc093d187d04c396f5c34f15e91fad04983e854e2d21c4d2b2ad1",
  "0xee4c050284a6279e0440819a796ed5368c892a686e8ecfd6f157f06af31af1c 1 0x368cf8b99408d61618eb8007a099dff52d440f002e40c323308e88cc6b69525 0x1a57cf22635b632f618144d6bed7f44bd8c4191cbbc114cc783ca9d2867208d3 0x9e67ddd9d0ab98c32bc668aebe2db0371af5db93f68cb187388cb78810cd558 0x189c83345b0e2a33dabb1e1f4946a6150383fdba9d16406702db595972f59bf0",
  "0x33d372857c3ffbd65735cfd8926e0b606664c6be1b1a115ee9ec3fc260780c1 1 0x1888ddbeb0df7d492b46126576e3303111206fa39f7ad25431c5403cb6d6aa88 0x67b06af8552645f83fd0f64a024f9613ff45bf688b97fc2eede7a624b07dc14 0x12fa9d4eef925fd147e759ae3853e783b07723683f02697f779f7c28cd41437b 0x5f4ac8b44286b6d17c1e5fd71569945d8fcd7da3606cfe65ac6f2b73c20cbd4",
  "0x12758f76b32871d926423556f8505ec034f63ab33601aeb5902dc407f723b75c 1 0x71a46132e6e0a7e89fc9d043ccb5e904bde15918b11aabffe67c57402ba0298 0x1ebc8b4300e0abf9c042e74de037bee77e24fff74e892b96a4dea820991252ef 0x83415732961e56d82d2adfc8d880bf45f0122cbd459c6eb7665c28c3bc1934f 0x53367b25854cfdb392cff2e0a0cb1ab67df9a1add3e853e11755d8c81342ba4",
  "0x1280dfc119d6bf64f197e903f4a850f5d3deaa02313e71513ca94ab986f5c936 1 0x21bb1687390be05ba5c1ac1b1c5a9a551875b2ab73fa944fd13bda9debb9b6b0 0x1350590e4e5b04da91a0f72ae60da0a9419e079d880f7b664d5dc7b9990f2a23 0x1cc12eb56eecd694765176a6c872caa51a5c57a60ced4b6328d1ccf87f2d816b 0x1fdd9a6047e5685cb5bb686f65e1513cd4c70c78c0b027b4fa93748637a0137d",
  "0xf1e36cddb32ce7e6a199d0ac286a3a3bdad8fab8c2c0351fff17f7e3bdda68a 1 0x19898dfab73524dc1d6adbb14de586b8564079e98b856db22225e1e03c2183df 0x153bd873337510aa7310df8650afb018edeec9dbe1848429994700cac25021ec 0x16ba68c95859258d5968dcca9dcf1dd262679b8d7242c09b9377f3a6c01ce374 0x1c8ba91b2e927694be74f32b2ab3657f663c0ccaecdb7256d0fcd9128c217d5d",
  "0x4d366ae25ff4cf3fea42ac8def92cce8ca6a5b89a79dc9a891b8a08908f8b45 1 0x12e6bdc97a2c2c3bbc053218178586b39949b67b5095f095abda8620be417b0d 0xe1fc4cc27e3d6009b219fd0358222347b614c29e1b2ce582f89cd1227611b95 0x3e0dee01c974959a640392186ecfb1a94386a6aacd7c61d4a7708dbddd6cf97 0x13534b90f77038bc9e1b669cb03a98b0c2d1c4d963b5538222608d6858149f0d",
  "0x17bcedac4930e5f9f26319f5d6710a850540d5d11a09e68a9271eb91f17a857a 1 0x74ae44896d387c3371cd7386a69023ba209f9a3c8529e8722c79dd02ac1d1e0 0x4f75ce120676699de498d6f8647c3c23bf5fd415d071c97a937dcc1a52f65bb 0x7d4cb2b31dd1bb297fffb634983d8df456a5c86d5ebbf938e1f7fc0501160b2 0x6b7db069cb78275e7ddfbf9e5eb24b5b027129ee3382f7f0e87f15ccc302a5e",
  "0x87d28291561214f2b8cc43b47d6a88ee726481b1f474cc233f49bde795c19df 1 0x1e523cd736ecca165752fc1ae48126f5ca28986c56d2c4e5b077419d3b4ec4d0 0x1a1938c8af0a624332ab759256da1ea7f9136fc288844d1d22df59be80f156ef 0x149b9e639f3677bdadbae110db955b82df03a74d2e4881fbf42d0ffaa1deb423 0x1ea8c689d774ef99daa4e6929eac2ce85aeab3170a4e067518124abc094c34b0",
  "0x1433b6647b139e834b68b69ff55f5c81fd933e515d6f23ba1ae9488a75b120b3 1 0x1db5b9281f9466b80de688a10d4f2bbd05513801261eaacd6cdcdefb4e4e2a31 0x79a85e4379aa38dc8a35cf16810d1ff0ecc4bda4e469d68a036cc8ebef2bbd3 0xd5d0649806757a85fb07cc2b07995db5835b459bf7bdc2e468ca23640a8a2b0 0xd10e4a46ee74f870ff7870f26c5c871ed5b4d653e4ca385c9111861a33ccdff",
  "0xc875aea2846b69dcd7442212a01b521f59f6584f4059b31e0b39aff93b393a1 1 0xa32c4cc5a5cda612386719134f0f3ff9f842224ad17e0d109af540da2527a9d 0x1798b3579592e4e374cd58eef5eb1099783c0ffffd51c7d5c04c1d5a4d34fee 0x3b7ca557207d4eb093f0c7ed5f3f8475fe98bade3263c9b4e863154c6f58534 0xdeac618efe74c3fafc8716b3b41958f7dc90ed4cf733fa75ca4e7da7e8d7a0c",
  "0x14686c3254ba0489b02d3699864067be872579e623f201272181b5dee5684278 1 0x19c11fc4df1a1af8e519325775faf94c971c5d7c1144a62abd27c07f48daccc4 0x253e89c7ff2002ff85059b3bceb179a6762ed7137fc5d0a6c6ffc7323c0af7f 0x1a1606a3a4df5a0ca2444328c03bbd7e8aadf959c66497271b537eb161986d51 0x1dcd4ed934d39701997340db5992839f75a92bcb3c4fb32ddc1b5e9cb7a9769e",
  "0x9099217eb391bcc8e4c539e42fc8e7b7ad1b5a8610e03eb86b8c406e337b962 1 0x6dd66ae7308bf58b26047605f80e11a038a7dc4db3d0816fbd83eff1b868083 0xd72486516b548e8f4354a93957e00c08d2164c6bd377852e6c54a22e46f702a 0x23599be039dcda5c31c77f72ddb92f08ea42bbb9f9b5e236ddaf9973df6f70ea 0x865f72a59901ebfe7f5452c6e2ef443a2298f7fac5d83c3f14b307ae56c3eda",
  "0x3941403966aaaa76306758e1b383ac762565906eb07f8bc548e368c164805db 1 0x145f3ff6c521890aad34cf093ce886b80b345582bfd59be0cbf2e8f07e589fbe 0x7538c15209f5c790412310118555f032b1a2d2b1c8999dae64d888deee74bdf 0x5720569359450ca482dd3e62b9eb7ecd33db32e6f68677a86eeed20f43ff5c 0xcbf91857c0790090128b9ed26f47b4fb8282d10a67c76044a965183cbead710",
  "0x24b7e8cc47ab1a3861469fdc251f0dcd5fe61022f20f5ec2c4d25216de038af2 1 0x2067de17bbba56e2e4501b868a55005dfa8bfab902a0fed2d9eea0c3885180e0 0x1edf0842aadbcbf6933599ca1c091cf3d1e26943e2230da920b9adcfbf8c0480 0x23970591bef89a65583cbf5721262029a854ce866b1576c993d8c9af45ddbc4e 0x197aa42fca116624c08df8d8e31174df61c45114ff4bb12335d63df2a988c668",
  "0xc3946b4a1c5572bb4e33108789ab7ff3476c99870f61a7f8122139c1bc3457b 1 0x1dd3f5a559e6640d0ccd2d1dc5fd76fd6fc41e5e209cfcbc4cb4fe973fe14de5 0x16578d4a05becf7d5fb0489b8d80749755d9a7056966f8e821b7e1b1eddf95b2 0x1b86e21a4f440cd034e2362e89771115c153d7af5d07cb91698802d1db3e061f 0x17976ec79abed427e42aed313afd3fc55500643b35ce2c95fd8bdbc4d1a961ed",
  "0xf2fab0c9d747228d7933f3e2da87df26a82fd7b8d6e4000ad60fe40be256fa1 1 0xe0200c90baaa84d1921703208c6c3c5e6b220eb043f1667d617bea96896e91a 0x2483f98fbbec1663f59f9e9a29deb5e51f928472a3c02c8cedc25c96d4c281d3 0x10b15e5b04b55deb49b133bc13e065be073ec7794844eac1eb8cc1f76910c209 0xa7215044830bf6497e2843b3df206088e950555f1d4cf22e2b15544c30fc04d",
  "0x23f5f2d18a8146310217beaba9a2c851e7bd0a883848af7695102a872adbbdf4 1 0x18baf3bdfdb6c728c158af2b26415d6b7003a4bc2d97b550b41a65eeb0a9ea6d 0x3b8b0cffd34052d306e65c560b835bd8cf39f9d2f7d6a6a68ecbed1fcb15dd2 0x14e6b6d97b6d7140656073c194d1d4d0b1c37d5be23c93cad69d5c337f07e04c 0x1276f5eda1940f2a9c8d25f9c3e0b10da5580cec5548133e36d2e35461e97202",
  "0x1a27432b235357dd0cac3471b5a45a21f24d010497cc0cb37f44e5b9173a8249 1 0x7c457d36e1d36b92cab8ed4662b111d59491a87b26e2fda1cceb34b274df0c3 0xc59db0bff680646ee92239da4bc347ca28cb163edbacdc53b8d6b0430fdad39 0x115d06b97ba8a3d94d14321bb3a366dd6c31acb9f10a2fabe3f83c2094e56130 0x78379bffc25b765d4dbc1d485d29697536343e97c0425a3c7cab35722b796ef",
  "0x6fe60da103ac791df9fc5013ebe95787918746690ad4db5b86aecd5be70568a 1 0x6a50a0aa03e4411ae386f733f05de7ce215643c24fd972789dcbc6cbff54942 0x27dd0f3995400ec1a2ad22d811ca85a588ec14984daa4054a2d00acb163b141 0x233d21c3d46c5b595c724c1cb3f30746c0bb3c59e1c33f4a778b04a15be8c756 0x236094be6d53f908f4f2f272074abbcaa94837f43b1bbf097e9dc8a464244d2e",
  "0x2253ccdda8c8d6c61ded88dbbe5adc2394d56a7df5d1798283b1f0cb6102f08d 1 0x1fbd3a6029fde05d5878c87d83b8711fab3bf6cd7eec2189eeba89bfe011c370 0x20c04d17793e9eea61f6736c29cdd952510b2ca0c2ad68d59a6a9be4aa34c8d0 0x2802cc24f3458c59cb687336b8ccda968f5cff6f4ffb7ee32460e008610f949 0x1cb7fd5dd38aa00cc8d221f92251d95276a27cb36748c9cf2ea0ecf0795ca7e8",
  "0x14036c05440291f98fb24edce0a3b21f70d44531b5f11687e0e1834e53875d94 1 0xce16f5d7dc2bace925ab15482b279faf049573feef015d7e3b26d8eef4a98a4 0xfb7fb217405f1e02bc6fc6371361c3ab02a94c35d129d0431be0171bfdb8cf0 0x94ff5df49c2c8070d8783bde5fce7740cbd72257a6eed8d709074cc3c2fcc39 0xd8194a98ae1958b13f0d279a6a6cb11ab11775f80c429d69fb11bf600c4658a",
  "0x74ad84afefb10196a2b43c0241504636092d6ebc30b42910b5fdf445c9202d3 1 0x192b9a1248ca3066a07b0282d862ee69b153a3b5ae89adff4e2cfa9d82949c3f 0x14790f2e92a507b09877aec0e939d14ad05f8bb2c7ea2c1429d9a213a3b29f7f 0x1ee693842e34152bbfdf865ddd137c636e0dd2857c83defbe1ca6ebdd814f26a 0x6a219df4f16ceb43ef455fe2ee6ee5516c1ccfd553e37d109f257235ddfdc89",
  "0x85a8911b10aa4ea699e7c46c123a093a8e2a195fa99a9e83f8b698303894603 1 0x930883066a831fad94aa501234ef72ce88b1e73f54d3e333ad3b4175c86b65 0x35457af504a24327c186a206752d9d4274b18901e64c80e8b37324bba73b2aa 0x1069c606fdc892808563cc3c954115aa2ed25f1a780f47a97a03f58e48f91f41 0xafb746115bb208761c287272da6a3c5734888d2246a2dd7352f8417bd88757d",
  "0xda6874dd1b2fcc46316fd4d5307acb145e85bc12c64020656a7a66fa6a306ef 1 0xaab38f0f46b122371111009a0c8b4b07ae4dfcc71ee58e90bc87c377006977a 0x1364a5c1dfeb079c40f4d37e5e0412c3717afa01093cd3c073369c154074285 0x13c953b425bdd9cd9ee8148a8fbc0b34edbcf4db354a8ad6d577e199efe4cdb1 0x20d61c5f8cd53a6e1fdaa421ee576c8233e98f7e1f9a6c4c87d0d62353a6728b",
  "0x39a223d0e645b92c5cc606ee9f30ddbe796d98dfae69bbf0b066c7e959d7c8d 1 0x1bfd799e8b4e41d41ff0c8655e23dc1d9f4036e9cb6ea7d375c3136458d73b9 0x84b85f54efd5199b00cb597af34e31048055fd276a297d9ab155e3ff2155702 0x92844b516c360b438882689292b93e9f95f74842938b99bc9310cdc75358a6f 0x178f30efa5664423c79558457eaf3e005d0d890989034dc2252889b0c18117c3",
  "0x5ac5f8cbaa2c4dddb56d0d66cb69dfca770b1ab1c1f0b645e41cb734a26b3ab 1 0x9b1a401f56ea8c7c6e5673653077639e0a76bc149fc54b9bf1950d53766e51c 0xd46c454e37e60295bd3bc0b537fa4f8b7e8312daceb7de1116b51826763b0cd 0x17c73c2e8318a0614a6601ecb7c4ce432eb4e933a69f11a844c080147a9ffb06 0x6e369fbcc4968934b67464d5d30c38b9f0b611c452ef06123b4ac468a7b6e18",
  "0xf7778469fbfd59cb4dc3c6d8c6c10fd5fb959db29dee5846f22f8908afb2d88 1 0x1cfdf6eace44763140203eb265ca1e90bb94af882eee80aaead5c206a53204f7 0x1c389cdaa3919ab44fbb5c7a616acd82bfeed1711149f75d0f7df6eb234a758e 0x23e1bf582ecd57dc2ae7b9031060a701c44ecee06182ad7b5b425f738f30bbb8 0x99d23f1a3383383f8c344ea0e3c8a684a00a196c9c00cc8310c2c0d567a6912",
  "0x200bd3f51f65c7dc1d127b8ea5721a3c4f8ed253b21659f771ba8b510a55364e 1 0x13eefc916e9cf8531977ed28b58e7ad587063712463026907eef4181396fc1cb 0x117c82ad7c9d3f9131a223b948a0f475044e8c02cf85b3a1b91b9a6a7ad11ebe 0x1787c697acb4c67693c5dafb2abaef8173bdea532cdcbd2c491e069c558e3fc7 0x134610fed66eb24b5d7e79c8e5bd3dd3a2a7b848e1489e1dac6bfcc5199d07dd",
  "0x1034198244697663cd95140b639eeed36f829fd266568670d6a2812af009124f 1 0x5d2a61819e7f4312285596e1b09f5f723920980ecd9c2bbf4a48fd86b2744c3 0x4cdeb38ee1953367fa4661f26f5de612b5f103db95be995522e83b2cdf1a9a9 0x1c9fdd701309c77338f37163e4b2bd45d8b32547c5a504439be9dc7b71972d48 0x22c287c80b9857e5f46a04000c376a4293f9e9359529915cdc4d4ec42e65a660",
  "0x4d2bfe4cf07f2fc81891056d6e782168feaf38b9c7e0a22b80fd9b0d2bcd13f 1 0x122709330d33acd5e19ae1091e656d5d98a039d012bc2369802bc9299fe118b8 0x4e8bcd9bb530266bb07306840f2bc9598121a5c1f95f664c1211f5d17ec27c4 0x24bc90d026c3162c48345ca1c04f017dd54e77a9c5a922da1febe329ecf7b61d 0x1aea30bf02237932d22476eba1de8ad2924f44c9c77858d72e30f1803f582216",
  "0x46bfce548819a0222ee80cbb05ec88eedb78cfb4f3c668ed78e8862dc432a9f 1 0x1ffd117ec58724938412e825d7e198b89a0789b1a4871e2c9734cfa485e082d9 0x1b78508a861df2bf61322d59b68e3475faf031670ebf5978db85fcfe7146c73e 0x166e67352cc9cb75704e6266cbe25e3aa78668f287c6c5ecb383287e764d0cc0 0x105c73177dd31c8ea16a3fdc9d94f2396329a4893e9d0db39959f2a0159b2551",
  "0x17b47f126c0536097cbe1de51b20cb6a1559daf0f7cfeb51c129e57f7b091c1 1 0x1002a570fc22efb1cab8acf78eeafecb7537b9c9290dbdb02f7b39376b09b5ac 0x3d5d26cc8f65e29ea3e1aabf283e996bbf80fab165b79d158f888721383074b 0x231fa1552138250c3feb4f0317aa9fde5c10cd4a68a49e3ab492a711d5db7bc 0x21940ecb5bbba2da11c693bcca04ba6fedcdd22b0f39ad878723bc1435752a69",
  "0x21e490159ff246fa34a3cf214bd3644d839cfbf79357b623f874b99e0b0ff1bb 1 0xb43a5421b94a21caf5d14ba025edd357156f3966b2a96927d51a9320a294415 0x190461ad0c11708b368e568c1d27666471b9d89ea34e177dc531983708dd6b70 0x10ffe04d1b63123977b6b1d04a373554b65cc189ce95b1bc65d0d1fad995b393 0x23033a4cfc8632945b204a08c86b859abb8308b51b186489a53bbd76243965b1",
  "0x1c5b797239fa3ed9b4272ad47f8b3f246aba0a7576cad03e52eee5faad5d8e30 1 0x1dc0bed5edb8b8497739bfb6fe3d3a8898a913521dca4c5041e56c168ec0684e 0x1da56508199086879bd2a2c4db58a4c747f4c45da383b0378281b25dff9d7531 0x96d81cc17d93576e16251c356eaecc2bfc73e8a6601c53a16191500211372cb 0x1520654e3d6a1e62b174267eb73f9d997dea2415d5096d100a030fdd55af3e71",
  "0x15fee874e3b4131275a92e0ff1df3d046752c5bb57e9451b9eb3def9d69a089f 1 0xf50919a165c0c1204438db9f0c38d6e996c311830ae35dec02f13f55a05d8b9 0xf761435c7b76e5075c46fb708666d8f863f194604ce792d30697ca5b6e44d09 0x38542068609d971cf5f57b9103e42e59bfe28244d63435cbdb7c6c3e20eb0e4 0x183c66772f579784da00c280978fc2d86c19da668f9a4d46b65f862968e9b5ed",
  "0x1b4e82ab7b9933ad850812211c27086fc644c97a10f51d6084706123af412a8d 1 0x12e4f38b4712efcd1d5a50317fa54bedb45bcb75cae81bf8bcc7e95df7044278 0x16319b8e5b5f79ecda95e1d33d1e41872bb6cdd655440210e0f3c19887db55ba 0x181c78ba5f7fddceabbe5d0e896830db294d5c2d6626e841da4a3f30c8436cf5 0x4a30e4cc4da2db30787d56b30fb84020f5d19127efa9f1a23b101cdaf703450",
  "0x203f7c634cac8d31c00d3b32b529d5a591139a87a5dcfaccb62773512f0d07cf 1 0x1e5f9293d634d32a7abaf2f1005e262f76de7cd310bbe101a49a04f2b750b391 0x220efd8edc01b2f1de8c170cb289e4645b430e98d4a1d0190e4f01b18bc2cc50 0x1c79b4c1860ed9f2b64bf23ad1e6118573f21d06c27dc7c7631656b8569aa516 0xa1ba40e758b5ecac48654ebdc354b908caf79c4b1c741bf9f60523c248807d4",
  "0x164f05fbe741f8d7e653e83db88341208809053e5168774c2c5250b69db7768b 1 0x507d4d33bcd422bccbd39da60bf3943fd16a9c8cbb0f7f0b71c3b02c06bb052 0x147d6f13c07b3e57a3404f7bfce56a3c30fadc3eafc208c05b1d5d8ef478e0ed 0x94c8e1273a15f5711597ea824ca2a7063fed4ee62f4d9e878a58263b15eec39 0x106f0ff1c0bdc2c1f4c187f761635f33cbe1c9624c603c81ad3123e4a314be1c",
  "0x87fa14f34043302bafce9a593b2872d900e6147feee16cc476166ea4e9a9ce3 1 0x651a8e49e0eaca2b2ef60ef62eee6bb18a67e6c88dc03d3c7c0ea47380789ab 0x1ef0d6ee96dc7efde1c18c9bf4897818ef91f8320761e3f7e63bff181e7f4b7e 0x18016396505e8932906c71eb5d2706a7505be7dcbb4a7d4b0abefbd8a4e25f00 0xd21a39d4112babe42139a4af089ca8c62660b0a30fd596f20fafe3692330ead",
  "0x14214084e58dbcc834dcacd3640543a989508a9109cbe841e4a23e825408453d 1 0x23c0a97cee7885d31b2a6cef3a503671029bdee7d873e2175198f0ab1d1980d4 0x166c666ef2ffeff35cba5d50e098c0f2549e83fdab5dfe7b94c8d41c186649a4 0x2470b0866666b29ffe8ec60c52b64b95d2288c7d2390705639fb3614644d4a23 0x55915f0523dd0d2d7221bf4964025524127ad104c7217f3fa3fe41e5a75a95a",
  "0x59a8f6a502f347efcdf0059bae0e76fde112fa19b11c6197df2d9f464b9fef1 1 0xc938a8443b26d911c3e6304c00db0e71b1b76b420ddff3adc9add45c535a677 0x1ec91c94ebdba8d7d56e4bc4ff9e5566e221e01b60f772dea24f8df713c560eb 0x10e9c1d42ff09bfccb61d3ccba36bdc42134c8c0100996b39364f75c0f9aa0fd 0x19dac9065ab3593d8060e5fe294c07b5d355f78cc68a4e835c056a28687c58f1",
  "0x26872c0d80aa88b51f5f09e6650e56b6541e5cd3c3621191ce9ff5b0952d3ca 1 0x149cc2b082bb8d690e0e5c15cb669aeb617ed16a53f0360505b44ab98693eb65 0x115a49c98174dd56f67e33135b49798804814502775dfc0523c5f605250e47ea 0x1b2de7eae4c7b21f12f079d4f6f856b006c6c0316e80482ffd9d829802ec63b2 0x1d97cc776853d82d96f39ce44b5840e98536bbcf2a5489f3f635965f9a2cd16e",
  "0x22070003ac000336443cda50a870a1becda6171876f089d821c41239c39e4e0f 1 0x20ae656436d02f045c314b3d9f138d4f40580ddb6dd289b88357689321f96733 0x3c6aa9440d749fd7d5286450734d829d65bf854c69d162d8b7a1655cdcd8205 0x42db4f76d0cad8d9400f192d20229de0011059ef906dd4ac455e393e8154cf5 0xe5f2c7534d2b063a924c6b36df02559d500c0269f448c96712be496532a2f75",
  "0x17d99fc9880cebba808ed10837a83584eebbac918b42f621804dcb76b9748e35 1 0x123b1ccc48c55dfae18419c1ec4758f23b85cfc3ac43934291e5451e9aa8b531 0x13d67ec82b18f52eb80eeb92e23801a02a89ed488be3575d63e807eadf38d8d6 0xeea2f95f6130b7cb5a256126d6c0f2ab368b34e027176cc22771f3fa80f5f70 0x1fca5630d6a248ca413bbe3b09050a5cf72cef879af15aae547b74b3e2e18e1c",
  "0x1d21c824e5ebcf99e8a589972414e7362d0d45c215cbe616c479192268850c7 1 0x12616aebf4f0d8dda7c5664f11a8f58900fd14e3da1c40a70dfecea46922edd2 0x14a37b3c539fdbf960007dea50e8d788fb703f23aa8654ebe55c4e45f7e87fc8 0x152db23e875f895d374be85a166ba8306b9a401937f9eb580da5b61244c05c3f 0x24c49e336aaa83e3bbd65dc238f894b2057ec87fd6245ff8ba831e2e49f9ed9a",
  "0x2489768d01656b7472cd1d3c39f565d43dd30329212f191765cfc380371456 1 0x23d7b7bb357cc3fd0559f8bd8a86bb23e2e091e21f452131e7184291c9ae166d 0x24ff8da73b5afc7a409946b551482b8e44c0e5388ff5d3ad124ca5536e1c902f 0x16cbcae1f9b172254aa00f50d8317055908c4d60ff33fbc243b560b0ad8d6878 0x44951f96aa7b2b3095dc07c595e5937c4d364cd2314278966de7b08836b7b8",
  "0x20bf3308919ab253aac0eb9f237dfb16dae4d6f935998e46595ac61c628c7e44 1 0x16f5451b1bfea80dc53205ad4ab21fcffd59428a81e5e3742c3060f445bacdee 0x15d492f279e02e92352f255a67fc6f48d02dcb1ddf26a7233c3c67eb7d266465 0xfab223cab1ea9723ba8fea1d30474535a8816ff0adbe0312c847e06a466bf69 0xb31b7dc034780817e79bd4f24dbd75817329c9823cc94db77fe283817e47739",
  "0x11227a1bd0b8059d46b5a96f691f952b566b310cce368c12fafe174ba266a886 1 0x2260b9f9be55a9c453dd22fe69e810844167882d25a59b2a4a34c43437b5282d 0x2498d21585b4165d4d1a2e3be4c525ed1de8e5e248a6c382f169e0579ae9632b 0x8d907671145892c059c1dc0d182fa525297fed83cc8958ef55f2ef53cef8e0e 0x5aafd32f2df8961fe9b8009ee3a5067ff0bd8eabb2ec23ee734693df946a15f",
  "0x1033d590de834b8f538dc74070f6f8c59e052b37c470dfb56be2668936b46292 1 0x18fcc28d3dc697eca7b56597d5cc6c6717ee465cd316a1e1147d10b74210a876 0x13f682c5ca0f514e9f846b3f4f60d64757d0b460ddfe4c366826a3a8cba0f079 0x158928750058974416538d5435f8f70908f3a85dad46054731d51afe63a9a30 0x2093bbc8319d76964a661e2674ea18d9ca8d905cd9e7792ba7dc6aa887228193",
  "0x20c59f304c26ce0445cd052cdba716e778b657c64637791654c907b29e02199 1 0x766713c3cfd9b51c87d1a7fd3c1d227cf4fc57a1397c33a15cc97130e2fdbb9 0x2f6abbfbcc14fa8f15db1ebaabd6ca5861231f20f58355bde735a8f34635596 0x15998df45deecc15b746a01899fe8254cbc00cdd9a9b71d85abb3dd2b8954901 0x1cdd1b71c642c48dfac636c04a2c3a940d0747c63fe76732869bec8c8875dbe5",
  "0x6392867964a4a26735151445b6b6d19184641933d1756c4dd0d1636ddf9b4d6 1 0x23d7b3f71a4d73e3ade182e6f330882cb435619cf6c200997e377ba68e763958 0x1dc71374eeb80c43b5e814f6a597df8b70bd68fc17980220712d26064ee6b151 0x172177e30110f729176f9bda71f7c5f80a01a567b24cd3f56b0d40023819edb6 0x1b88b3f40ebb63994bc53511a56bd8a99171a464066e3cbaa9ba1957971ba9d6",
  "0x23d72a778bc0604a4566b52a7ec279d2d2a21b05d4a70c7102907eeb1f243264 1 0xc4eff5595ae063eb59e7ba7eca82a48fbb3313c010006b3a3160b4925bc4532 0x1fa315dadbf6595d8147511d6a8970c050aa33d4e3fa8007bb2cf4c16cfc26f5 0x13046c0750a5fc36b3b3b55c70477de332378318c37836725c85c743f2745560 0x31b4638cea0c622741d7f10090c624a05dee148a0af6142946dee75d80169dd",
  "0x6668bbb61fc53971bb9b2f0d4b7c2eda4c263a7a18fcb5e86c6bb0a40dccb20 1 0x2225beb39151a5ae594c829ab67ed2c5d635484541fe18e9b2cb2e150ae415fd 0x20aebe0c0cc20c6b0afbf490a7751ef2fc85c23ff401897d8dc02f127f865598 0x15a506a4d1a9b551b58e409ffed365c517b3973385d33b0264bfe7964fd6ab75 0x2d48f4f71853df3e2c413cb42381383dda96ee7c980606593968ccbc119dee6",
  "0x19639f4e1aaaaa841709ef6a31b37a080f66e0c3835ca94d8dca73937761e1be 1 0x113661d92b5e2ab2c0c31472846a907d7c5e0ecd9a229a137f671b6d505e4b88 0xec51ca188f507003f3b59ecd11ca92456ba069ca212f1b100beb580894f0279 0x1b987f6082fdb5a5adfcb364e42ee17861a07cb5f256170a2210d948dfbd6779 0x85f58068543842517dc52246bd1f4356e3a1c3520cfd87d5b8789d8945a79d1",
  "0x40e7a0a77151866aac8fea5d9ec6e902d62a73bb17c5b0cdd653ea15d89a334 1 0x1582397f8b4fcf6e10cd4480e55cfce3cee6146aae400af8eff7eaa64cbac73c 0x2335104fa864402f7051892a0d72a7c778839969d1273c9c5041eb268a76a2ea 0x9319c6fbfd2f1cfdd63c8c38a4a01de389323e674ea2a620462285707d34413 0x13260495bb0ab8b8acf1a8628c74a08ed433e89826ef3522ae71d140f34d45e4",
  "0x1df7eef412561817ad64e6231079ed05519bb7170aae6188fc7ed21512d5eef7 1 0x1aeca76cda5931f2e8892f08324904b9a2878da2f51aa9b7d22336c0b1bf5f64 0x1a0ca7a0281fbe0418df054ebf5abd8d38dce969e7be4f51e3b2ce1e5a64f84c 0x1ab9eac10a137ad109b5d32c7271df39a5cdfdc4bc0a4dbbb5cbe74efb0fcab3 0x1b982cad3bb83c3564052c4844c16dc26bea48870933a87f739c5e84e6e70b73",
  "0x15c408436fa7d3c07bfcfd467e8ba6ad6abd5a7a3796599e8719bbcec93b4c3b 1 0x7085b18c86830ed0f4b1499ad4016267451ee8d6df9ca9488cf2e60204e7dfe 0x1a1808be60924ba0c2f3594e32ba6505ad44d1ddbe97d3944a5fa307d22eb293 0x1ab312d49f968d5ad5e589600fa1e7014a82debb5cc5cb4d79cda0f3e7272b3e 0xc1c98439a02993d99c0b97dac524a029e36333cdd497afedf83bb30d42888d1",
  "0x73467e8d44cc18da7308f1c51cd85796799d7af1deb0e7a654d0460ba8aacf7 1 0x13c250d37ec531b6c20bbeb359e0d061c4637696ba3cf5e811f10194c04b3e4c 0x1252947a79deed0e593de68cc351a59f5009a42f809c8249aeabe68490db81ed 0xdf4f8cea3c4afb57ed1ab458d816183abf9be1586677753a0196de34e743d0b 0x1c90abd93a849ef9f3bcb78e55fb55de58c7e86c6ca126c972678426ba6dda20"};

using boost::multiprecision::uint128_t;
using namespace boost::multiprecision::literals;

uint8_t DelegateIdentityManager::_global_delegate_idx = 0;
AccountAddress DelegateIdentityManager::_delegate_account = 0;
DelegateIdentityManager::IPs DelegateIdentityManager::_delegates_ip;
bool DelegateIdentityManager::_epoch_transition_enabled = true;

DelegateIdentityManager::DelegateIdentityManager(Store &store,
                                         const Config &config)
    : _store(store)
{
   Init(config);
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE HARD-CODED PUB KEYS!!! TODO
void
DelegateIdentityManager::CreateGenesisBlocks(logos::transaction &transaction)
{
    BlockHash epoch_hash(0);
    BlockHash microblock_hash(0);
    MDB_txn *tx = transaction;
    // passed in block is overwritten
    auto update = [this, tx](auto msg, auto &block, const BlockHash &next, auto get, auto put) mutable->void{
        if (block.previous != 0)
        {
            if ((_store.*get)(block.previous, block, tx))
            {
                LOG_FATAL(_log) << "update failed to get previous " << msg << " "
                                << block.previous.to_string();
                trace_and_halt();
                return;
            }
            block.next = next;
            (_store.*put)(block, tx);
        }
    };
    for (int e = 0; e <= GENESIS_EPOCH; e++)
    {
        ApprovedEB epoch;
        ApprovedMB micro_block;

        micro_block.delegate = logos::genesis_account;
        micro_block.timestamp = 0;
        micro_block.epoch_number = e;
        micro_block.sequence = 0;
        micro_block.last_micro_block = 0;
        micro_block.previous = microblock_hash;

        microblock_hash = _store.micro_block_put(micro_block, transaction);
        _store.micro_block_tip_put(microblock_hash, transaction);
        update("micro block", micro_block, microblock_hash,
               &BlockStore::micro_block_get, &BlockStore::micro_block_put);

        epoch.epoch_number = e;
        epoch.timestamp = 0;
        epoch.delegate = logos::genesis_account;
        epoch.micro_block_tip = microblock_hash;
        epoch.previous = epoch_hash;
        bls::KeyPair bls_key;
        auto get_bls = [&bls_key](const char *b)mutable->void {
            stringstream str(b);
            str >> bls_key.prv >> bls_key.pub;
        };
        for (uint8_t i = 0; i < NUM_DELEGATES; ++i) {
            get_bls(bls_keys[0]); // same in epoch 0, doesn't matter

            std::string s;
            bls_key.pub.serialize(s);
            DelegatePubKey dpk;
            memcpy(dpk.data(), s.data(), CONSENSUS_PUB_KEY_SIZE);
            Delegate delegate = {0, dpk, 0, 0};
            //            memcpy(delegate.bls_pub.data(), s.data(), CONSENSUS_PUB_KEY_SIZE);

            if (e != 0 || !_epoch_transition_enabled)
            {
                uint8_t del = i + (e - 1) * 8 * _epoch_transition_enabled;
                get_bls(bls_keys[del]);
                char buff[5];
                sprintf(buff, "%02x", del + 1);
                logos::keypair pair(buff);
                delegate = {pair.pub, dpk, 100000 + (uint64_t)del * 100, 100000 + (uint64_t)del * 100};
            }
            epoch.delegates[i] = delegate;
        }

        epoch_hash = _store.epoch_put(epoch, transaction);
        _store.epoch_tip_put(epoch_hash, transaction);
        update("epoch", epoch, epoch_hash,
               &BlockStore::epoch_get, &BlockStore::epoch_put);
    }
}

void
DelegateIdentityManager::Init(const Config &config)
{
    logos::transaction transaction (_store.environment, nullptr, true);

    _epoch_transition_enabled = config.all_delegates.size() == 2 * config.delegates.size();

    BlockHash epoch_tip;
    uint16_t epoch_number = 0;
    if (_store.epoch_tip_get(epoch_tip))
    {
        CreateGenesisBlocks(transaction);
        epoch_number = GENESIS_EPOCH + 1;
    }
    else
    {
        ApprovedEB previous_epoch;
        if (_store.epoch_get(epoch_tip, previous_epoch))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::Init Failed to get epoch: " << epoch_tip.to_string();
            trace_and_halt();
        }

        // if a node starts after epoch transition start but before the last microblock
        // is proposed then the latest epoch block is not created yet and the epoch number
        // has to be increamented by 1
        epoch_number = previous_epoch.epoch_number + 1;
        epoch_number = (StaleEpoch()) ? epoch_number + 1 : epoch_number;
    }

    // TBD: this is done out of order, genesis accounts are created in node::node(), needs to be reconciled
    LoadGenesisAccounts();

    _delegate_account = logos::genesis_delegates[config.delegate_id].key.pub;
    _global_delegate_idx = config.delegate_id;

    ConsensusContainer::SetCurEpochNumber(epoch_number);

    // get all ip's
    for (uint8_t del = 0; del < 2 * NUM_DELEGATES && del < config.all_delegates.size(); ++del)
    {
        auto account = logos::genesis_delegates[del].key.pub;
        auto ip = config.all_delegates[del].ip;
        _delegates_ip[account] = ip;
    }
}

/// THIS IS TEMP FOR EPOCH TESTING - NOTE PRIVATE KEYS ARE 0-63!!! TBD
/// THE SAME GOES FOR BLS KEYS
void
DelegateIdentityManager::CreateGenesisAccounts(logos::transaction &transaction)
{
    // create genesis accounts
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        stringstream str(bls_keys[del]);
        bls::KeyPair bls_key;
        str >> bls_key.prv >> bls_key.pub;
        logos::genesis_delegate delegate{logos::keypair(buff), bls_key, 0, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);

        uint128_t min_fee = 0x21e19e0c9bab2400000_cppui128;
        logos::amount amount(min_fee + (del + 1) * 1000000);
        logos::amount fee(min_fee);
        uint64_t work = 0;

        StateBlock state(pair.pub,  // account
                         0,         // previous
                         0,         // sequence
                         StateBlock::Type::send,
                         pair.pub,  // link/to
                         amount,
                         fee,       // transaction fee
                         pair.prv.data,
                         pair.pub,
                         work);

        ReceiveBlock receive(0, state.GetHash(), 0);

        _store.receive_put(receive.Hash(),
                receive,
                transaction);

        _store.account_put(pair.pub,
                           {
                               /* Head    */ 0,
                               /* Previous*/ 0,
                               /* Rep     */ 0,
                               /* Open    */ state.Hash(),
                               /* Amount  */ amount,
                               /* Time    */ logos::seconds_since_epoch(),
                               /* Count   */ 0,
                               /* Receive */ 0
                           },
                           transaction);
    }
}

void
DelegateIdentityManager::LoadGenesisAccounts()
{
    for (int del = 0; del < NUM_DELEGATES*2; ++del) {
        char buff[5];
        sprintf(buff, "%02x", del + 1);
        stringstream str(bls_keys[del]);
        bls::KeyPair bls_key;
        str >> bls_key.prv >> bls_key.pub;
        logos::genesis_delegate delegate{logos::keypair(buff), bls_key, 0, 100000 + (uint64_t) del * 100};
        logos::keypair &pair = delegate.key;

        logos::genesis_delegates.push_back(delegate);
    }
}

void
DelegateIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx)
{
    Accounts delegates;
    IdentifyDelegates(epoch_delegates, delegate_idx, delegates);
}

void
DelegateIdentityManager::IdentifyDelegates(
    EpochDelegates epoch_delegates,
    uint8_t &delegate_idx,
    Accounts & delegates)
{
    delegate_idx = NON_DELEGATE;

    bool stale_epoch = StaleEpoch();
    // requested epoch block is not created yet
    if (stale_epoch && epoch_delegates == EpochDelegates::Next)
    {
        LOG_ERROR(_log) << "DelegateIdentityManager::IdentifyDelegates delegates set is requested for next epoch";
        return;
    }

    BlockHash epoch_tip;
    if (_store.epoch_tip_get(epoch_tip))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch tip";
        trace_and_halt();
    }

    ApprovedEB epoch;
    if (_store.epoch_get(epoch_tip, epoch))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch: "
                        << epoch_tip.to_string();
        trace_and_halt();
    }

    if (!stale_epoch && epoch_delegates == EpochDelegates::Current)
    {
        if (_store.epoch_get(epoch.previous, epoch))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get current delegate's epoch: "
                            << epoch.previous.to_string();
            trace_and_halt();
        }
    }

    LOG_DEBUG(_log) << "DelegateIdentityManager::IdentifyDelegates retrieving delegates from epoch "
                    << epoch.epoch_number;
    // Is this delegate included in the current/next epoch consensus?
    for (uint8_t del = 0; del < NUM_DELEGATES; ++del)
    {
        // update delegates for the requested epoch
        delegates[del] = epoch.delegates[del].account;
        if (epoch.delegates[del].account == _delegate_account)
        {
            delegate_idx = del;
        }
    }
}

bool
DelegateIdentityManager::IdentifyDelegates(
    uint epoch_number,
    uint8_t &delegate_idx,
    Accounts & delegates)
{
    BlockHash hash;
    if (_store.epoch_tip_get(hash))
    {
        LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch tip";
        trace_and_halt();
    }

    auto get = [this](BlockHash &hash, ApprovedEB &epoch) {
        if (_store.epoch_get(hash, epoch))
        {
            LOG_FATAL(_log) << "DelegateIdentityManager::IdentifyDelegates failed to get epoch: "
                            << hash.to_string();
            trace_and_halt();
        }
        return true;
    };

    ApprovedEB epoch;
    bool found = false;
    for (bool res = get(hash, epoch);
              res && !(found = epoch.epoch_number == epoch_number);
              res = get(hash, epoch))
    {
        hash = epoch.previous;
    }

    if (found)
    {
        // Is this delegate included in the current/next epoch consensus?
        delegate_idx = NON_DELEGATE;
        for (uint8_t del = 0; del < NUM_DELEGATES; ++del) {
            // update delegates for the requested epoch
            delegates[del] = epoch.delegates[del].account;
            if (epoch.delegates[del].account == _delegate_account) {
                delegate_idx = del;
            }
        }
    }

    return found;
}

bool
DelegateIdentityManager::StaleEpoch()
{
    auto now_msec = GetStamp();
    auto rem = Seconds(now_msec % TConvert<Milliseconds>(EPOCH_PROPOSAL_TIME).count());
    return (rem < MICROBLOCK_PROPOSAL_TIME);
}

void
DelegateIdentityManager::GetCurrentEpoch(BlockStore &store, ApprovedEB &epoch)
{
    BlockHash hash;

    if (store.epoch_tip_get(hash))
    {
        trace_and_halt();
    }

    if (store.epoch_get(hash, epoch))
    {
        trace_and_halt();
    }

    if (StaleEpoch())
    {
        return;
    }

    if (store.epoch_get(epoch.previous, epoch))
    {
        trace_and_halt();
    }
}
