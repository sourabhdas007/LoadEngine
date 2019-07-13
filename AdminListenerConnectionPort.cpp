
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
// $Author: jayanta.sahoo $
// $Revision: 175672 $
//
/////////////////////////////////////////////////////////////////////





/*
Description:: This object is responsible for connect to the Admin and all so this object mount the east directories.
*/
#include "AdminListenerConnectionPort.hpp"
#include "Trace.hpp"
#ifdef win32
#include <process.h>
#else
#endif
#include "Environment.hpp"

#include "TCPUtilities.hpp"
Notice* AdminListenerConnectionPort::notice_;
/*
Ashok: Object definition, describe all the objects, and at least brief description of each 
method . Change the name to AdminListenerConnectionPort to be more clear
*/
AdminListenerConnectionPort::AdminListenerConnectionPort()
{
	isAlive_ = true   ;
	notice_ = new Notice("");
	logManager_  = 0x0 ;

}

int AdminListenerConnectionPort::write(const char* data, const int length,int id) 
{
	notice_->setPayload(data,length);

	try{
		notice_->write(fd_);
	}
	catch(...)
	{
		int error = TCPUtilities::getSocketError();
		#ifdef WIN32
			if (error == 10054)
		#else
			if (error == ECONNRESET)
		#endif
		stop();
		IFLOG(logManager_,LoggerManager::Level1)
		{
			string tmp = "Connection reset . ";
			logManager_->logString(tmp,LoggerManager::Level1);
		}
	}
    return 1;	
}

void AdminListenerConnectionPort::read(char* &data, int& length) 
{
	notice_->read(fd_);
	length = notice_->length();
	data = (char*) notice_->payload();
}

void AdminListenerConnectionPort::processRead() {
	char* data ;
	int length;

	try{
		read(data, length);
	if ( manager_ == 0x0 )
		;
	else 
 	  manager_->received(this,data, length);
	}catch(...){
		stop();
		TRACE(Trace::Debug,"AdminListenerConnectionPort::processRead() :: errorNo :: ");
		TRACELN(Trace::Debug, errno);
		isAlive_=false;
		IFLOG(logManager_,LoggerManager::Level1)
		{
			string tmp = "Connection reset . ";
			logManager_->logString(tmp,LoggerManager::Level1);
		}
	}
}

string AdminListenerConnectionPort::toString()
{
	string result= "";
	result +=adminId_  ;
	result +=loginUser_;
	return result;
}


/////////////////////////////////////////////////////////////////////
//
// $Log$
// Revision 1.9.42.1  2006/04/04 22:22:53  asahu
// Merged from bR40-3-b02062006-PLATFORM
//
// Revision 1.9.64.1  2006/03/22 18:25:49  asahu
// Fixed to display the login user name in ReservedBy field.
//
// Revision 1.9  2004/09/08 17:46:51  asahu
// Send the attach event to load.exe
//
// Revision 1.8  2003/12/29 00:03:55  asahu
// Fixed the memory leak for KEEP_ALLIVE notice .
//
// Revision 1.6  2003/11/18 18:34:46  sbehera
// Fixed exception in Admin Listener
//
// Revision 1.5  2003/07/18 18:55:02  asahu
// Added code to execute the load.exe at server machine in case of regression .
//
// Revision 1.4  2003/02/06 16:13:06  sbehera
// Fixed the signarure of write method - Ashok sahu
//
// Revision 1.3  2003/01/16 20:49:38  sbehera
// called  stop()  of the port if  the  server sockect is closed - Ashok Sahu
//
// Revision 1.2  2002/12/18 17:21:56  sbehera
// Fixed the compilation error - Ashok sahu
//
// Revision 1.1  2002/11/14 20:47:11  aagarwal
// support admin for traffic runner
//
//

