#include <logos/identity_management/ntp_client.hpp>

#include <stdlib.h>
#include <unistd.h>

#include <thread>
/**
 *  NTPClient
 *  @Param i_hostname - The time server host name which you are connecting to obtain the time
 *                      eg. the pool.ntp.org project virtual cluster of timeservers
 */
NTPClient::NTPClient(string i_hostname)
        :_host_name(i_hostname),_port(123),_ntp_time(0),_delay(0)
{
    //Host name is defined by you and port number is 123 for time protocol
}

/**
 * RequestDatetime_UNIX()
 * @Returns long - number of seconds from the Unix Epoch start time
 */
long NTPClient::RequestDatetime_UNIX()
{
    return NTPClient::RequestDatetime_UNIX_s(this);
}

long NTPClient::RequestDatetime_UNIX_s(NTPClient *this_l)
{
    time_t timeRecv;

    boost::asio::io_service io_service;

    boost::asio::ip::udp::resolver resolver(io_service);
    boost::asio::ip::udp::resolver::query query(
            boost::asio::ip::udp::v4(),
            this_l->_host_name,
            "ntp");

    boost::asio::ip::udp::endpoint receiver_endpoint = *resolver.resolve(query);

    boost::asio::ip::udp::socket socket(io_service);
    socket.open(boost::asio::ip::udp::v4());

    boost::array<unsigned char, 48> sendBuf  = {010,0,0,0,0,0,0,0,0};

    socket.send_to(boost::asio::buffer(sendBuf), receiver_endpoint);

    boost::array<unsigned long, 1024> recvBuf;
    boost::asio::ip::udp::endpoint sender_endpoint;

    try{
        size_t len = socket.receive_from(
                boost::asio::buffer(recvBuf),
                sender_endpoint
        );

        timeRecv = ntohl((time_t)recvBuf[4]);

        timeRecv-= 2208988800U;  //Unix time starts from 01/01/1970 == 2208988800U

    }catch (std::exception& e){

        std::cerr << e.what() << std::endl;

    }

    this_l->setNtpTime(timeRecv);
    return timeRecv;
}

void NTPClient::timeout_s(NTPClient *this_l)
{
    int count = 0;
    static const int MAX = NTPClient::MAX_TIMEOUT;

    while(true) {
        if(this_l->getNtpTime()) {
            return;
        }
        if(count++ > MAX) {
            break;
        }
        sleep(1);
    }

    //std::cout << "NTPClient::timeout_s udp socket timed out\n";
}

void NTPClient::asyncNTP()
{
    _ntp_time = 0;
    std::thread t1(NTPClient::RequestDatetime_UNIX_s,this);
    t1.detach();
    std::thread t2(NTPClient::timeout_s,this);
    t2.join();
}

bool NTPClient::timedOut()
{
    if(!_ntp_time) {
        return true;
    } else {
        return false;
    }
}

time_t NTPClient::getTime()
{
    return _ntp_time;
}

time_t NTPClient::getDefault()
{
    return (_ntp_time=time(0));
}

void NTPClient::start_s(NTPClient *this_l)
{
    while(true) {
        this_l->asyncNTP();
        if(this_l->timedOut()) {
            this_l->setNtpTime(0);
            this_l->setDelay(0);
        }
        sleep(60*60); // Sleep for one hour.
    }
}

time_t NTPClient::init()
{
    asyncNTP();
    std::thread t1(start_s,this);
    t1.detach();
    return computeDelta();
}

time_t NTPClient::computeDelta()
{
    if(timedOut()) {
        if(_delay) {
            return _delay; // Previous delta.
        } else {
            return (_delay=0); // Zero.
        }
    }
    // compute new delta.
    return (_delay=(abs(time(0) - _ntp_time)));
}

time_t NTPClient::getCurrentDelta()
{
    return _delay;
}

time_t NTPClient::now()
{
    return time(0) + _delay; // Our time + ntp delta.
}