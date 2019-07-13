//
// $HeaderURL$
//
/////////////////////////////////////////////////////////////////////
// Copyright:    Copyright 2002-2005, IPNetFusion, Inc.
//               All Rights Reserved.
//               This material is confidential and a trade secret.
//               Permission to use this work for any purpose must be 
//               obtained in writing from:
//
//               IPNetfusion, Inc.
//               3400 Waterview Pk, suite 100
//               Richardson, TX 75080
//
// $Author: sahooja $
// $Revision: 164009 $
//
/////////////////////////////////////////////////////////////////////



#ifndef AdminListenerConnectionPort_hpp
#define AdminListenerConnectionPort_hpp
#include <string>
#include <map>
#include "NetworkTCPServerConnectionPort.hpp"
#include "StreamBuf.hpp"
#include "LoggerManager.hpp"


class AdminListenerConnectionPort:public NetworkTCPServerConnectionPort
{
public:
	static Notice* notice_ ;

	string adminId_  ;
	string loginUser_;
	string eastHome_;
	string javaHome_;
	string osname_;
	string ipAddress_;
	vector<string> osCommandList_;
	LoggerManager* logManager_ ;
	string serverName_;
	AdminListenerConnectionPort();
	int write(const char* data,const int length , int id = -1); 
	void read(char* &data, int& length) ;
	void processRead();
	string toString();
	bool  isAlive_ ;
};
#endif

/////////////////////////////////////////////////////////////////////
//
// Revision 1.4  2003/12/26 21:22:53  asahu
// Supported to run multiple TrafficRunner for a remote machine .
//
// $Log$
// Revision 1.6.54.1  2006/04/04 22:22:53  asahu
// Merged from bR40-3-b02062006-PLATFORM
//
// Revision 1.6.76.1  2006/03/22 18:25:57  asahu
// Fixed to display the login user name in ReservedBy field.
//
// Revision 1.6  2004/01/12 22:09:03  asahu
// Displayed  the response of the AdminListener in status dialog .
//
// Revision 1.5  2003/12/29 00:03:55  asahu
// Fixed the memory leak for KEEP_ALLIVE notice .
//
// Revision 1.3  2003/07/18 18:55:02  asahu
// Added code to execute the load.exe at server machine in case of regression .
//
// Revision 1.2  2003/02/06 16:13:06  sbehera
// Fixed the signarure of write method - Ashok sahu
//
// Revision 1.1  2002/11/14 20:47:11  aagarwal
// support admin for traffic runner
//
//

