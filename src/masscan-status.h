#ifndef MASSCAN_STATUS_H
#define MASSCAN_STATUS_H

#if 0
enum PortStatus {
    Port_Unknown,
    Port_Open,
    Port_Closed,
    Port_IcmpEchoResponse,
    Port_UdpOpen,
    Port_UdpClosed,
    Port_SctpOpen,
    Port_SctpClosed,
    Port_ArpOpen,
};
#endif

enum PortStatus {
    PortStatus_Unknown,
    PortStatus_Open,
    PortStatus_Closed,
    PortStatus_ZeroWin, /*Recv a SYNACK with zero win*/
    PortStatus_Responsed, /*ACK our req of app-layer with data in stateless mode*/
    PortStatus_Arp,
    PortStatus_Count

};



#endif
