#ifndef CONTROLLERLED_HPP
#define CONTROLLERLED_HPP

#include <frem\rpc.hpp>

class ControllerLed : public frem::RpcService<ControllerLed>
{
public:
    ControllerLed();

    void Tier1();


    FREM_RPC(Code(0x0001B109),
             Alias("InstrumentAPI_Controller_Illumination_enable"),
             Tags("ForwardOnMaster"))
    void enable(bool on);
};

#endif // CONTROLLERLED_HPP
