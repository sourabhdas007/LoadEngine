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
// $Author: dhaval.modi $
// $Revision: 206350 $
//
/////////////////////////////////////////////////////////////////////


//Description::This application is runs in a Remote Machine. Wait for request for status reserve the machine, start/stop desktop.


#include "AdminListener.hpp"
#include "AdminListenerCommon.hpp"
#include "ObjectPool.hpp"
#include "UtilFunc.hpp"
#include "Trace.hpp"
#include "EventProducer.hpp"
#include "System.hpp"
#include "TCPUtilities.hpp"
#include "Environment.hpp"
#include "StringTokenizer.hpp"
#include <algorithm>
#include "CommonUtilFuncs.hpp"
#include "Integer.hpp"
#include <ifaddrs.h>
#ifdef WIN32
#include <process.h>
#include "SystemUtils.hpp"
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>    
#include <arpa/inet.h>
#include <errno.h>
#endif
#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <stdio.h>
#include <signal.h>
#endif
#ifndef WIN32
#include <AssignIPV4AndV6.hpp>
#include "IPUtility.hpp"
#include "HSLog.h"
#include "NetLinkUtils.hpp"
#endif
#include <memory>

#include "std.h"
#include "os.h"

int gChasissNum=0;
//typedef int INT32;
//typedef char   CHAR;


#define IFLIST_REPLY_BUFFER 8192
//Ashok: 08212002: Why we are not using the NetworkTCPServer or some derived class
//where most of the code to handle TCPServer is already there. I want to break up 
//The TCP to be base class?
//Ashok A :Typically, multiple IP address may exist in the listen machine
//Ashok s: derive the same from NetworkTCPServer  . maintain a list of Connections .

//Ashok A; 0828:New things:Add new events, when the process started: 
//Status:ProcessStarted,   send processId and applicationName.
//When traffic runner starts, it can maintain the the processId of load.exe. If it is restarted,
//then it will make sure it is killed.
//In distributed mode, the desktop running on that machine will make sure the load.exe is killed.
//If the "exit" is selected, it can make sure all the process is killed, before it exit.
//The process list also will be with adminListener, which will make sure all the process are killed
//before started desktop again.

//THis is very critical that, we kill any application running, otherwise, it will be very difficult
//to run in a distributed manner. Those needs to be captured in the requirement.
//Another requirement is timing synchronization we need to think about. Multiple machine should
//be synchronized on time. let's caputre this in the requirement, I will be coming up with ideas
//how to handle it. You are welcome to write ideas.
//About mount: In windows you can use 
//net use  localdrive  //hostname/share_drive_name

//SA 0902: Look into tags for comments with tag SA
//Ashok sahu : I will update the code as per the suggetion of Sunil A . 
//Ashok 0907: Need object description, and at least brief description of each
//method
//chinmaya 1210 modify the receive event to receivevent non blocking 
//chinmaya 0523 send status info in Keep alive notice 
//Sourbah::082605::Added code to close all the port if admin exit
#define MAX_NO_OF_LE_ARGS 128

#ifdef WIN32
#else
#define stricmp(left,right) strcasecmp(left,right)
#endif

#ifndef WIN32
AssignIPV4AndV6 ipaGenerator_;
#endif

AdminListener* adminListener=0;
#ifdef __RM__
void SignalHandler(void)
#else
void SignalHandler(int iSigNum)
#endif
{
}

#ifdef WIN32
void closeDesktop(DWORD type)
{
	if( type == CTRL_C_EVENT ||
		type == CTRL_BREAK_EVENT || 
		type == CTRL_CLOSE_EVENT)
	{
		if(adminListener && adminListener->isDesktopRunning_)
			adminListener->sendStopDesktopRequest();
		exit(0);
	}
	return;
}
#else
void closeDesktop(int signum)
{
	if(adminListener)
	{
		adminListener->dumpStartnEndInfoToLogFile(STOP);
		if(adminListener->isDesktopRunning_)
			adminListener->sendStopDesktopRequest();		
	}
	exit(0);
	return;
}
#endif

void AdminListener::start()
{
	NetworkTCPServerPort::start();
}

AdminListener:: AdminListener(string host, int port)
{
	source_.host = host;
	source_.port = port;
	initialize();
}

void AdminListener::initialize()
{
	desktopStarted_= false;
	checkLocalMachine_ = true;
	manager_ = this;
	currentAdmin_ = 0;
	ownerId_ = "NONE";
	localconsumer_ = 0x0;
	localproducer_ = 0x0;

	timerMgr_= new TimerMgr;
	Timer *timer = Timer::newObject();
	timer->setDuration(2000); //2 seconds 
	stopDesktoptimer_ = RunningTimer::newObject();
	stopDesktoptimer_->setAttributes(timer,this,-1,ADMIN_TIMER::STOP_DESKTOP_TIMER);

	timer = Timer::newObject();
	timer->setDuration(30000); //30 seconds 
	KeepAliveTimer_ = RunningTimer::newObject();
	KeepAliveTimer_->setAttributes(timer,this,-1,ADMIN_TIMER::KEEP_ALIVE_TIMER);
	timerMgr_->startTimer(KeepAliveTimer_);


	timer = Timer::newObject();
	timer->setDuration(10000); //10 seconds 
	statusTimer_ = RunningTimer::newObject();
	statusTimer_->setAttributes(timer,this,-1,ADMIN_TIMER::STATUS_TIMER);
	timerMgr_->startTimer(statusTimer_);

	timer = Timer::newObject();
	timer->setDuration(10000); //5 seconds 
	desktopStatusTimer_= RunningTimer::newObject();
	desktopStatusTimer_->setAttributes(timer,this,-1,ADMIN_TIMER::DESKTOP_STATUS_TIMER);
	timerMgr_->startTimer(desktopStatusTimer_);

	timer = Timer::newObject();
	timer->setDuration(30000); //30 seconds 
	atrStatusTimer_= RunningTimer::newObject();
	atrStatusTimer_->setAttributes(timer,this,-1,ADMIN_TIMER::ATR_STATUS_TIMER);
	timerMgr_->startTimer(atrStatusTimer_);

	timer = Timer::newObject();
	timer->setDuration(20000); //20 seconds
	restDesktopStatusTimer_= RunningTimer::newObject();
	restDesktopStatusTimer_->setAttributes(timer,this,-1,ADMIN_TIMER::REST_DESKTOP_STATUS_TIMER);
	
	version_ = 1;
	status_ = ADMIN_STATE::AVAILABLE;
	current_state_ = ADMIN_STATE::AVAILABLE; //new 

	logManager_ = new LoggerManager;
	isDesktopRunning_ = false ;
	invokedesktop_ =true;
	
	consumer_=0;
	producer_=0;
	clientMode_ = false;
	loadServerName_ = "";
	runnerType_ = -1;
		
	isSocketExits_= false;
	netLinkFd_ = -1;
	//deviceIndex_ = 0;
	#ifndef WIN32
	 struct rlimit tl;
	 int result;
	 tl.rlim_cur = 64*1024;
	 tl.rlim_max = 64*1024;
	 result=setrlimit(RLIMIT_NOFILE,&tl);
	 struct rlimit tl1;
	 tl1.rlim_cur = RLIM_INFINITY;
	 tl1.rlim_max = RLIM_INFINITY;
	 result=setrlimit(RLIMIT_CORE,&tl1);
	#endif
	netInterfaces_ = "";
	cardNumber_=0;
	cardNumberLE_=0;
	cardNumberSWDG_=0;
	loadCardNumber();	
	initRMInterface();
	getHwListFromRM();
}

auto to_string(const npg_rm::ResManNPGClient::PortInfo& info) {
    return "Port Info { card number " +CommonUtilFuncs::itoa(info.osCardNum) + ", Port type " +CommonUtilFuncs::itoa(info.portType)+ ", disp name " \
	+ info.displayName + "}";
} 


void AdminListener::getHwListFromRM(void)
{
	portInfoVector_.clear();
	npg_rm::ResManNPGClient::ErrorCode errCode = rmClient_.getHwResourceList(portInfoVector_);
	if(errCode == npg_rm::ResManNPGClient::OK)
	{		
		for (const auto &e:portInfoVector_)
			dumpRMLog(errCode, to_string(e));
	}
	else
	{
		string log = "Get hardwire list failed";
		log+="Error:: getHwResourceList returns "+rmClient_.getDetailErrorMessage();
		dumpRMLog(errCode, log);
	}
}
void AdminListener::setEventServerHostPort(string& hostname , int port)
{
	eventServerHost_ = hostname ;
	eventServerPort_ = port ;
	consumer_  = new EventConsumer(eventServerPort_,eventServerHost_ ,0);
	producer_ = new EventProducer(eventServerPort_,eventServerHost_);
			
	Timer *timer = Timer::newObject();
	reconnectTimer_  = RunningTimer::newObject() ;

	timer->setDuration(1000); //1 seconds 
	reconnectTimer_ ->setAttributes(timer,this,-1, ADMIN_TIMER::RECONNECT_TIMER);
	timerMgr_->startTimer(reconnectTimer_ );
}

void AdminListener::setLogLevel(int level)
{
	logManager_->setLogLevel(level);
}

void AdminListener::setLogFile(string logfile)
{
	logManager_->setLogFile(logfile);
	logManager_->setLogFileStream();
}

string AdminListener::getLoadServerName()
{
	return loadServerName_;
}

AdminListener:: ~AdminListener()
{

}

//I am thinking, we can keep a list of AdminConnectionPort;
//Ashok A: Need to understand that when adminListen is started, the east
//directories are not mounted on that machine. If not
//mounted, we need to mount.
//Then if the current directory is not path_, we need
//to change to that directory.
//Also, I am thinking that we need to ask it to mount java files (That is future).

//Ashok A :you need to exit the application which was
//started.
//Ashok s: If we kill the desktop then all the process that is running within the desktop 
//may not killed . So i think to send a  notice to the current 
//desktop to stop the desktop . so please suggest . 
//Ashok:: I am thinking, that we need to put intelligent here; it should
//kill all the java.exe, load.exe and other .exe.
//For that, I think I had send a code which queries if the process
//exist, if exist, then kill it.
//When the desktop starts, it can send out the process ids as 
//an event, similarly the load,exe
//When desktop is started, the admin register with the event server
//for process Ids of all the server, 
//Then make sure it kills all the process ids;
//That way we will make sure all the prcess are killed before another
//load.exe starts
//SA lets break the routine into individual blocks of code 


/*
 Ashok: 1024: THere is a good chance that it may lose connection with 
 admin?? We need to implement keep alive to make sure the connection is up??
 Also, if the connection is broken? the admin may restart, will send the admin id;
 then it will re-attach, implement attach procedure??
*/

void AdminListener::received(NetworkPort* port, char buffer[],int length) 
{
	AdminListenerConnectionPort* admin = (AdminListenerConnectionPort*)port;
	int request_type ;
	string adminId;
	int version ;
	istr_.reset();
	istr_.setBuffer((char*)buffer,length);
	istr_.get(version);

	if(version >= 50)
	{
		handleRESTCommand(admin);
		return;
	}	
	
	if (version != version_)//first check to see if it is current version
	{
		IFLOG(logManager_,LoggerManager::Level1)
		{
			string tmp = "Receive command having invalid Version";
			logManager_->logString(tmp,LoggerManager::Level1);
		}
		ostr_.reset();
		ostr_.put(version);
		ostr_.put(ADMIN_CMD::FAILURE);
		string result = "Invalid version..";
		ostr_.put(result);
		ostr_.put(version_);
		char* buff ;int length ;
		ostr_.getBuffer(buff,length);
		admin->write(buff,length);
		return ;
	}
	istr_.get(request_type);
	if(request_type > ADMIN_CMD::EASTFLEX_REQUEST)
	{
		handleEastFlexRequest(admin,request_type);
		return;
	}
	istr_.get(admin->adminId_); //get the Admin Id;
	istr_.get(admin->loginUser_); //get the Admin Id;
	int  level = LoggerManager::Level5; 
	if(request_type == ADMIN_CMD::KEEP_ALIVE )
	{
		level = LoggerManager::Level6;
		IFLOG(logManager_,level)
		{
			string tmp = "Received Request =";
			tmp +=request2String(request_type);
			tmp +=":: Admin Id =";
			tmp +=admin->adminId_;
			logManager_->logString(tmp,(LoggerManager::Levels)level);
		}
		admin->isAlive_ = true ;
		if (admin->adminId_  == ownerId_ && currentAdmin_)
			currentAdmin_ = admin;
	}
	else if (admin->adminId_  == ownerId_ ) // Ashok s: check the ownerId 
	{
		switch(request_type)
		{
			case ADMIN_CMD::START_DESKTOP:
			{
				istr_.get(admin->osname_); // where is the exe, etc
				istr_.get(admin->eastHome_); // where is the exe, etc
				istr_.get(admin->javaHome_); // where is the exe, etc
				istr_.get(admin->serverName_);
				int count ;
				istr_.get(count);
				admin->osCommandList_.clear();
				for ( int i = 0 ; i < count ; i++ )
				{
					string command ;
					istr_.get(command);
					admin->osCommandList_.push_back(command);
				}
				startDesktop(admin);
				break;
			}
			
			case  ADMIN_CMD::STOP_DESKTOP:
			{
				stopDesktop(admin);
				break;
			}
			case ADMIN_CMD::UNRESERVE:
			{
				unreserve(admin);
				break;
			}
			case ADMIN_CMD::RESTART:
			{
				bootServer(true);
				break;
			}
			case ADMIN_CMD::SHUTDOWN:
			{
				bootServer(false);
				break;
			}
			case ADMIN_CMD::GENERATE_IP:
			{
				if(runnerType_ == ADMIN_RUNNER::EastflexRunner)
				{
					handleEastFlexGenerateIP(admin);
					break;
				}
				string device;
				string netMask;
				string startingIP;
				int noOfIP;
				int ipType;
				int prefixVal;
				int noHexCharFlagVal;
				istr_.get(device);
				istr_.get(ipType);
				if (ipType == ADMIN_IP::IP_TYPE_IPV4)
				{
					istr_.get(netMask);
					istr_.get(startingIP);
					istr_.get(noOfIP);
					generateIPV4Address(admin,device,netMask,startingIP,noOfIP);
				}
				else if(ipType == ADMIN_IP::IP_TYPE_IPV6)
				{
					istr_.get(prefixVal);
					istr_.get(startingIP);
					istr_.get(noOfIP);
					istr_.get(noHexCharFlagVal);
					generateIPV6Address(admin,device,prefixVal,startingIP,noOfIP,noHexCharFlagVal);
				}
				break;
			}
			case ADMIN_CMD::CLEAR_IP:
			{
				string device;
				istr_.get(device);
				clearIPAddress(admin,device);
				break;
			}
            case ADMIN_CMD::ROUTE_IP:
            {
                string device;
                string netmask;
                string gateway;
                string netIP;
                istr_.get(device);
                istr_.get(netmask);
                istr_.get(gateway);
                istr_.get(netIP);
                routeIPAddress(admin,device,netmask,gateway,netIP);
                break;
             }
             case ADMIN_CMD::ROUTE_IPV6:
             {
                 string netIP;
                 string gateway;
                 string device;
                 string prefix;
                 istr_.get(netIP);
                 istr_.get(gateway);
                 istr_.get(device);
                 istr_.get(prefix);
                 routeIPV6Address(admin,netIP,gateway,device,prefix); 
             }
			 case ADMIN_CMD::ROUTE_DEL_IPV6:
			{
				 string netIP;
                 string gateway;
                 string device;
                 string prefix;
                 istr_.get(netIP);
                 istr_.get(gateway);
                 istr_.get(device);
                 istr_.get(prefix);
				 clearRouteIPV6(admin,netIP,gateway,device,prefix);
			}
			 case ADMIN_CMD::ROUTE_DEL:
			{
				 string device;
				 string netIP;
				 string netmask;
				 string gateway;
                 istr_.get(device);
                 istr_.get(netIP);
				 istr_.get(netmask);
				 istr_.get(gateway);
				 delRouteIPV4(admin,device,netIP,netmask,gateway);
			}
		}
	}
		
	else if ( request_type  == ADMIN_CMD::RESERVE )
	{
			istr_.get(admin->eastHome_); // 
			istr_.get(ipRemoteMgntPanel_);
			reserve(admin);
	}
	else if ( request_type  == ADMIN_CMD::ATTACH )
	{
		string eventServerHost,eventServerPort,runnerHost,runnerID,serverName;  
		int runnerPort;
		istr_.get(eventServerHost);
		istr_.get(eventServerPort);
		istr_.get(runnerHost);
		istr_.get(runnerPort);
		istr_.get(runnerID);
		istr_.get(serverName);
		Notice  event("loadprofile:attach");
		OStreamBuf ostr;
		ostr.reset();
		ostr.put(eventServerHost);
		ostr.put(eventServerPort);
		ostr.put(runnerHost);
		ostr.put(runnerPort);
		ostr.put(runnerID);
		ostr.put(serverName);
		char* buff=0;
		int length=0;
		ostr.getBuffer(buff,length);
 		event.setPayload(buff,length);
		producer_->sendEvent_nonblocking(&event);
		attach(admin);
	}
	else 
	{
		string tmp = "Unable to process request , Server is reserved by another Admin , Admin Name = "+ownerId_ ;
		sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
	}
	
}

auto to_string(const npg_rm::ResManNPGClient::CardInfo& info,os_cardnum_t cardNumber) 
{
    return "OscardNumber:"+CommonUtilFuncs::itoa(info.cardNumber)+" for slot::"+CommonUtilFuncs::itoa(cardNumber);
} 
os_cardnum_t AdminListener::getPEOsCardNum(void)
{
	npg_rm::ResManNPGClient::stdVec_CardInfo cardInfo;
	npg_rm::ResManNPGClient::ErrorCode errCode = rmClient_.getCardList(cardInfo);
	if(errCode == npg_rm::ResManNPGClient::OK)
	{
		for (const auto &e:cardInfo)
		{
			dumpRMLog(errCode, to_string(e, cardNumber_));
			if(e.cardNumber == cardNumber_)
			{
				return e.osCardNum;
			}
		}
	}
	return OS_CARDNUM_UNDEFINED;
}

#if 0
void AdminListener::handleStopSoftDataGenApplication(Notice* notice)
{
	istr_.reset();
	istr_.setBuffer((char*)notice->payload(),notice->length());
	string identifier;
	istr_.get(identifier);
	TRACELN(Trace::Debug,"stopSoftDataGen request received for::"+identifier);

	map<string,int>::iterator itr;
	//itr = leProcess2ResourceID_.find("SWUserPlane");
	itr = leProcess2ResourceID_.find(identifier);
	if(itr != leProcess2ResourceID_.end())
	{
		//TRACELN(Trace::Debug,"stopSoftDataGen cardNumber"+cardNumber_);
		npg_rm::ResManNPGClient::ErrorCode errCode = rmClient_.stopAgentProgram(cardNumber_);	
		dumpRMLog(errCode,"stopAgentProgram");			
		errCode = rmClient_.destroyAgentProgram(cardNumber_);
		dumpRMLog(errCode,"destroyAgentProgram");
		errCode=rmClient_.deAllocateHwResource(itr->second);
		dumpRMLog(errCode,"deAllocateHwResource");
		leProcess2ResourceID_.erase(itr);
	}
}
#endif

void AdminListener::handleStopApplication(Notice* notice, bool cpuShared)
{
	istr_.reset();
	istr_.setBuffer((char*)notice->payload(),notice->length());
	string identifier;
	istr_.get(identifier);
	TRACELN(Trace::Debug,"stop request received for::"+identifier);
	os_cardnum_t cardNum = cpuShared ? cardNumberLE_ : cardNumberSWDG_;

	map<string,int>::iterator itr;
	itr = leProcess2ResourceID_.find(identifier);
	if(itr != leProcess2ResourceID_.end())
	{
			npg_rm::ResManNPGClient::ErrorCode errCode = rmClient_.stopAgentProgram(cardNum);
			dumpRMLog(errCode,"stopAgentProgram");			
			errCode = rmClient_.destroyAgentProgram(cardNum);
			dumpRMLog(errCode,"destroyAgentProgram");
			errCode=rmClient_.deAllocateHwResource(itr->second);
			dumpRMLog(errCode,"deAllocateHwResource");
			leProcess2ResourceID_.erase(itr);
	}
	
}
void AdminListener::dumpRMLog(npg_rm::ResManNPGClient::ErrorCode errCode, string operation)
{
	if(errCode != npg_rm::ResManNPGClient::OK)
	{
		string temp = "Error in "+operation+":: Error string :"+rmClient_.getDetailErrorMessage();
		TRACELN(Trace::Debug,temp);
	}
	else
	{
		string temp = operation+":successful";
		TRACELN(Trace::Debug,temp);
	}			
}

#if 0
void AdminListener::handleStartSoftDataGenApplication(Notice* notice)
{
	istr_.reset();
	istr_.setBuffer((char*)notice->payload(),notice->length());
	int argc , resourceId=0;
	//string appNamewithPath = "/east/chassis-111/EAST/SR1812_111/bin_linux/SWUserPlane.sh";
	string appNamewithPath;
	istr_.get(appNamewithPath);
	TRACELN(Trace::Debug,"appNamewithPath::"+appNamewithPath);
	cout << "san Sw appNamewithPath::"<<appNamewithPath<<endl;
	string leidentifire;
	istr_.get(leidentifire);	
	TRACELN(Trace::Debug,"leidentifire::"+leidentifire);
	istr_.get(argc);
	vector<string> args;
	args.reserve(argc);
	TRACELN(Trace::Debug,"argc::"+argc);
	cout << "san argc::"<<argc<<endl;
	auto arguments=unique_ptr<const char *>(new const char*[argc]);
	int i =0;
	for(;i<argc;i++)
	{
		string arg;
		istr_.get(arg);
		TRACELN(Trace::Debug,"SAN>> arg::"+arg);
		TRACELN(Trace::Debug,"argv::"+arg);
		cout << "san argv::"<<arg<<endl;
		args.push_back(std::move(arg));
		arguments.get()[i] = args.back().c_str();
	}
	//string async = "-async-start";
	//args.push_back(std::move(async));
	//arguments.get()[i+1] = args.back().c_str();
	for(int i=0;i<argc;i++)
	{
		cout << "argument::"<<arguments.get()[i]<<endl;
	}

	//Invoke Shell Script

	npg_rm::ResManNPGClient::HwResourceRequest hwResource;
	for(unsigned int i=0;i<portInfoVector_.size();i++)
	{
		int slotnum;
		sscanf(portInfoVector_[i].displayName.c_str(),"%u",&slotnum);
		if( cardNumber_ == slotnum)
		{
			hwResource.osCardNum = getPEOsCardNum();//portInfoVector_[i].osCardNum ;
			hwResource.portNum =  0;//portInfoVector_[i].portType;
			hwResource.peOsCardNum = 0;//getPEOsCardNum();
		}
	}

	hwResource.hwCpuMode = npg_rm::ResManNPGClient::CPU_OWN;
	hwResource.prgType = npg_rm::ResManNPGClient::PRGYPE_CPU;

	TRACELN(Trace::Debug,"allocateHwResource is called with");
	TRACELN(Trace::Debug,"                                 hwResource.osCardNum:"+CommonUtilFuncs::itoa(hwResource.osCardNum));
	TRACELN(Trace::Debug,"                                 hwResource.portNum:"+CommonUtilFuncs::itoa(hwResource.portNum));
	TRACELN(Trace::Debug,"                                 hwResource.peOsCardNum:"+CommonUtilFuncs::itoa((int)hwResource.peOsCardNum));
	TRACELN(Trace::Debug,"                                 hwResource.hwCpuMode:"+CommonUtilFuncs::itoa((int)hwResource.hwCpuMode));
	TRACELN(Trace::Debug,"                                 hwResource.peOsCardNum:"+CommonUtilFuncs::itoa((int)hwResource.prgType));

	npg_rm::ResManNPGClient::ErrorCode errCode = rmClient_.allocateHwResource(&hwResource,resourceId);
	dumpRMLog(errCode,"allocateHwResource return resource id = "+CommonUtilFuncs::itoa(resourceId));
	if(errCode == npg_rm::ResManNPGClient::OK)
	{
		//leProcess2ResourceID_["SWUserPlane"] = resourceId;
		leProcess2ResourceID_[leidentifire] = resourceId;

		TRACELN(Trace::Debug,"createAgentProgram called with");
		TRACELN(Trace::Debug,"                               hwResource.osCardNum "+CommonUtilFuncs::itoa(hwResource.osCardNum));
		TRACELN(Trace::Debug,"                               hwResource.resourceId "+CommonUtilFuncs::itoa(resourceId));
		TRACELN(Trace::Debug,"                               path"+appNamewithPath);
		errCode = rmClient_.createAgentProgram(resourceId,appNamewithPath,hwResource.osCardNum);
		if((npg_rm::ResManNPGClient::OK != errCode ) || ( hwResource.osCardNum < 1) || ("" == appNamewithPath))
		{
			if(hwResource.osCardNum < 1)
			{
				TRACELN(Trace::Debug,"OS Card Number :"+CommonUtilFuncs::itoa((int)(hwResource.osCardNum)));
			}
			else
			{
				dumpRMLog(errCode,"RM Error string for createAgentProgram");
			}
		}
		else
		{
			TRACELN(Trace::Debug,"CreateAgent sucessfull with OS Card Number:"+CommonUtilFuncs::itoa(hwResource.osCardNum));
			errCode = rmClient_.startAgentProgram(hwResource.osCardNum,argc, const_cast<char**>(arguments.get()));
			if((npg_rm::ResManNPGClient::OK != errCode ))
			{
		
		 		TRACELN(Trace::Debug,"allocateHwResource Failed");
				TRACELN(Trace::Debug,"startAgent failed with Error String:");
			}
			else
			{
				dumpRMLog(errCode,"startAgent is Sucessfull with RM returned string:");
				cardNumber_ = hwResource.osCardNum;
				//TRACELN(Trace::Debug,"startSoftDataGen cardNumber"+cardNumber_);
			}
			TRACELN(Trace::Debug, args.front());
		}
	}
	else
	{
		 TRACELN(Trace::Debug,"allocateHwResource Failed");
	}
}
#endif

inline void dumpargs(auto** argv, int argc) {
	for (int i = 0; i < argc; i++) {
		TRACELN(Trace::Debug, "argv::"s+argv[i]);
		cout << "ptr: " << (void*) argv[i] << '\n';
	}
}
void AdminListener::handleStartApplication(Notice* notice, bool cpuShared)
{
	istr_.reset();
	istr_.setBuffer((char*)notice->payload(),notice->length());
	int argc , resourceId=0;
	string appNamewithPath;
	string leidentifire;
	istr_.get(appNamewithPath);	
	TRACELN(Trace::Debug,"appNamewithPath::"+appNamewithPath);
	istr_.get(leidentifire);	
	TRACELN(Trace::Debug,"leidentifire::"+leidentifire);
	istr_.get(argc);
	auto arguments=unique_ptr<const char *>(new const char*[argc]);
	vector<string> args;
	args.reserve(argc);
	TRACELN(Trace::Debug,"argc::"+CommonUtilFuncs::itoa(argc));
	for(int i=0;i<argc;i++)
	{
		string arg;
		istr_.get(arg);
		TRACELN(Trace::Debug,"argv::"+arg);
		args.push_back(std::move(arg));
		arguments.get()[i] = args.back().c_str();
	}
	//Get update portInfoVector_ from Resource Manager
	getHwListFromRM();
	TRACELN(Trace::Debug,"Updated portInfoVector after getHwListFromRM"+portInfoVector_.size());
	npg_rm::ResManNPGClient::HwResourceRequest hwResource;
	for(unsigned int i=0;i<portInfoVector_.size();i++)
	{
		int slotnum;
		sscanf(portInfoVector_[i].displayName.c_str(),"%u",&slotnum);
		if( cardNumber_ == slotnum)
		{
			hwResource.osCardNum = getPEOsCardNum();//portInfoVector_[i].osCardNum ;
			hwResource.portNum =  0;//portInfoVector_[i].portType;
			hwResource.peOsCardNum = 0;//getPEOsCardNum();
		}
	}

	if(sysInfo_.boardType_.compare("vPE") != 0)
	{
		cpuShared = false; //On pQA we will pin loadEngine to single CPU
	}
	hwResource.hwCpuMode = cpuShared ? npg_rm::ResManNPGClient::CPU_SHARED : npg_rm::ResManNPGClient::CPU_OWN;
	hwResource.prgType = npg_rm::ResManNPGClient::PRGYPE_CPU;
	TRACELN(Trace::Debug,"allocateHwResource is called for "+sysInfo_.boardType_);
	TRACELN(Trace::Debug,"                                 hwResource.osCardNum:"+CommonUtilFuncs::itoa(hwResource.osCardNum));
	TRACELN(Trace::Debug,"                                 hwResource.portNum:"+CommonUtilFuncs::itoa(hwResource.portNum));
	TRACELN(Trace::Debug,"                                 hwResource.peOsCardNum:"+CommonUtilFuncs::itoa((int)hwResource.peOsCardNum));
	TRACELN(Trace::Debug,"                                 hwResource.hwCpuMode:"+CommonUtilFuncs::itoa((int)hwResource.hwCpuMode));
	TRACELN(Trace::Debug,"                                 hwResource.prgType:"+CommonUtilFuncs::itoa((int)hwResource.prgType));

	npg_rm::ResManNPGClient::ErrorCode errCode = rmClient_.allocateHwResource(&hwResource,resourceId);
	if(errCode == npg_rm::ResManNPGClient::OK)
	{
		leProcess2ResourceID_[leidentifire] = resourceId;

		TRACELN(Trace::Debug,"createAgentProgram called with");
		TRACELN(Trace::Debug,"                               hwResource.osCardNum "+CommonUtilFuncs::itoa(hwResource.osCardNum));
		TRACELN(Trace::Debug,"                               hwResource.resourceId "+CommonUtilFuncs::itoa(resourceId));
		TRACELN(Trace::Debug,"                               path"+appNamewithPath);
		errCode = rmClient_.createAgentProgram(resourceId,appNamewithPath,hwResource.osCardNum);
		if((npg_rm::ResManNPGClient::OK != errCode ) || ( hwResource.osCardNum < 1) || ("" == appNamewithPath))
		{
			if(hwResource.osCardNum < 1)
			{
				TRACELN(Trace::Debug,"OS Card Number :"+CommonUtilFuncs::itoa((int)(hwResource.osCardNum)));
			}
			else
			{
				dumpRMLog(errCode," RM Error string for createAgentProgram");
			}
		}
		else
		{
			TRACELN(Trace::Debug,"CreateAgent sucessfull with OS Card Number:"+CommonUtilFuncs::itoa(hwResource.osCardNum));
			errCode = rmClient_.startAgentProgram(hwResource.osCardNum,argc, const_cast<char**>(arguments.get()));
			if((npg_rm::ResManNPGClient::OK != errCode ))
			{
				dumpRMLog(errCode,"startAgent failed with Error String:");
			}
			else
			{
				dumpRMLog(errCode,"startAgent is Sucessfull with RM returned string:");
				os_cardnum_t &cardNum = cpuShared ? cardNumberLE_ : cardNumberSWDG_;
				cardNum = hwResource.osCardNum;
			}
			TRACELN(Trace::Debug, args.front());
		}
    }
}
void AdminListener::loadCardNumber(void)
{
	string fileName = "/var/run/iwnet.slot";
	std::ifstream in(fileName.c_str());
	if ( in.fail()  )
		return;
	else
	{
		while ( !in.eof() ) 
		{
			std::string strLine;
			getline(in, strLine);
			if(strLine.length() > 0)
			{
				cardNumber_	= atoi(strLine.c_str());
				sysInfo_.slotNo_ = cardNumber_;
			}
		}
	}	
}
void AdminListener::handleEastFlexRequest(AdminListenerConnectionPort* admin,int request_type)
{
	istr_.get(admin->adminId_); 
	istr_.get(admin->loginUser_); 
	istr_.get(admin->serverName_);
	if( request_type  == ADMIN_CMD::EASTFLEX_BLADE_INFO_REQUEST)
	{
		sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_BLADE_INFO_RESPONSE,"");
		return;
	}
	if ( request_type == ADMIN_CMD::EASTFLEX_RESERVE_REQUEST)
	{
		if(current_state_ == ADMIN_STATE::AVAILABLE)
		{
			reserve_EastFlex(admin);
		}
		else if (current_state_ == ADMIN_STATE::RESERVED )
		{
			string tmp= "Already reserved..";
			sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_ALREADY_RESERVED_BY_MASTER,tmp);
		}
		else
		{
			sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_ERROR,"Blade is already reserved by "+ownerId_);			
		}
			
	}
	else if ( request_type == ADMIN_CMD::EASTFLEX_UNRESERVE_REQUEST)
	{
		if(status_ != ADMIN_STATE::AVAILABLE && admin->adminId_ == ownerId_ )
		{
			unreserve_EastFlex(admin);
		}
		else
		{
			sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_ERROR,"Blade is reserved by "+ownerId_);			
		}
			
	}
	else if(request_type == ADMIN_CMD::EASTFLEX_START_DESKTOP_REQUEST)
	{
		if (admin->adminId_  != ownerId_ )
		{
			sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_ERROR,"Blade is already reserved by "+ownerId_);			
			return;
		}
		istr_.get(admin->ipAddress_);
		istr_.get(admin->javaHome_); 
		istr_.get(admin->eastHome_);
		startDesktopEastFlex(admin);
	}
	else if(request_type == ADMIN_CMD::EASTFLEX_HW_INFO_REQUEST)
	{
		sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_HW_INFO_RESPONSE,"");
	}
}


int AdminListener::handleEastFlexGenerateIP(AdminListenerConnectionPort* admin)
{
	int ret =1;
//	ipaGenerator_.ub_false_ = true; // this will ingone .255 and .0 IPAddress;

	string device,netMask,startingIP,ethType;
	int noOfIP,ipType,prefixVal,noHexCharFlagVal,size,deleteIP,noOfEntity,generateIPFlag,routeFlag;
	string netIP,gateway,prefix;
	string ethPort = "eth1";	
	if(stricmp(sysInfo_.boardType_.c_str(),"AT8060") == 0)
		ethPort = "eth5";
	else if (stricmp(sysInfo_.boardType_.c_str(),"TravelHawk") == 0)
		ethPort = "eth2";
	else
		ethPort = "eth1";
	int step;
	ipGenReq_.clear();				
	int generate_ip;
	char* buff;
	int length;
	istr_.get(buff,length);
	istr_.reset();
	istr_.setBuffer(buff,length);
	istr_.get(ethType);
	istr_.get(deleteIP);
	if(deleteIP)
	{
		istr_.get(size);
		for(int i=0; i<size;i++)
		{
			string device;
			istr_.get(device);
			if(stricmp(ethType.c_str(),"NPU") == 0)
				clearIPAddress(ethPort);//Clearing IPs from eth1
			else
				clearIPAddress(device);
		}
	//	clearIPAddress(ethPort);//Clearing IPs from eth1
	}
	istr_.get(size);
	for(int i=0; i<size;i++)
	{
		istr_.get(generateIPFlag);
		if (generateIPFlag)
		{
			istr_.get(noOfEntity);
			for(int i=0; i<noOfEntity;i++)
			{
				istr_.get(ipType);
				if (ipType == ADMIN_IP::IP_TYPE_IPV4)
				{
					istr_.get(device);
					istr_.get(startingIP);
					istr_.get(netMask);
					istr_.get(step);
					istr_.get(noOfIP);
					AutoIpInfo autoIpInfo;
					autoIpInfo.startIp = startingIP;
					autoIpInfo.device = device;
					autoIpInfo.maskVal = atoi(netMask.c_str());
					autoIpInfo.step = step;
					autoIpInfo.ethType = ethType;
					autoIpInfo.noOfIPs = noOfIP;
					autoIpInfo.ipType = "IPv4";
					ipGenReq_.push_back(autoIpInfo);
					string ethdev ="";
					if(stricmp(ethType.c_str(), "NPU") == 0)
					{
						generateIPV4Address(admin,ethPort,netMask,startingIP,noOfIP,step);
						ethdev = ethPort;
					}
					else
					{
						generateIPV4Address(admin,device,netMask,startingIP,noOfIP,step);
						ethdev = device;
					}
					
					string tmp = ethdev+" :Successfully generated IP Address...";
					sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_STATUS_EVENT,tmp);
				}
				else if(ipType == ADMIN_IP::IP_TYPE_IPV6)
				{
					istr_.get(device);
					istr_.get(startingIP);
					istr_.get(prefixVal);
					istr_.get(step);
					istr_.get(noOfIP);
					noHexCharFlagVal = 0;
					AutoIpInfo autoIpInfo;
					autoIpInfo.startIp = startingIP;
					autoIpInfo.device = device;
					autoIpInfo.maskVal = prefixVal;
					autoIpInfo.step = step;
					autoIpInfo.ethType = ethType;
					autoIpInfo.ipType = "IPv6";
					ipGenReq_.push_back(autoIpInfo);
					if(stricmp(ethType.c_str(), "NPU") == 0)
						generateIPV6Address(admin,ethPort,prefixVal,startingIP,noOfIP,noHexCharFlagVal,step);
					else
						generateIPV6Address(admin,device,prefixVal,startingIP,noOfIP,noHexCharFlagVal,step);
					string tmp = device+" :Successfully generated IP Address...";
					sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_STATUS_EVENT,tmp);
				}
				
			}
			istr_.get(routeFlag);
			if (routeFlag)
			{
				string ipVersion,tmp;
				istr_.get(noOfEntity);
				for(int i=0; i<noOfEntity;i++)
				{
					istr_.get(ipVersion);
					istr_.get(netIP);
					istr_.get(gateway);
					istr_.get(device);
					istr_.get(prefix);
					if(stricmp(ipVersion.c_str(),"ipv4") == 0)
					{
						routeIPAddress(admin,device,prefix,gateway,netIP);
						tmp = "Successfully added IPv4 route. ";
					}
					else
					{
						routeIPV6Address(admin,netIP,gateway,device,prefix);
						tmp = "Successfully added IPv6 route. ";
					}

					sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_STATUS_EVENT,tmp);
				}
			}
		}
	}
	if(netLinkFd_>=0)
	{
		close(netLinkFd_);	
		netLinkFd_=-1;
	}
	string tmp = "IP Generated successfully..";
	sendEastFlexResponse(admin,ADMIN_CMD::GENERATE_IP_RESPONSE,tmp);
	ipaGenerator_.ub_false_ = false;
	return ret;
}


int AdminListener::sendEastFlexResponse(AdminListenerConnectionPort* admin , int command , string result)
{
		int ret = 1;
		ostr_.reset();
		if(command != ADMIN_CMD::EASTFLEX_HW_INFO_RESPONSE)
		{
			ostr_.put(version_);
		}
		ostr_.put(command);
		ostr_.put(status_);
		ostr_.put(current_state_);
		switch(command)
		{
			case ADMIN_CMD::EASTFLEX_ERROR:
			{
				ostr_.put(ownerId_);
				ostr_.put(result);
				break;
			}
			case ADMIN_CMD::EASTFLEX_RESERVE_RESPONSE:
			{
				ostr_.put(ownerId_);
				//Chinmaya
				if(status_ == ADMIN_STATE::DESKTOP_STARTED)
				{
					ostr_.put(eventServerPort_);
					ostr_.put(eventServerHost_);
					ostr_.put(desktopPath_);
				}
				break;
			}
			case ADMIN_CMD::EASTFLEX_UNRESERVE_RESPONSE:
			{
				ostr_.put(ownerId_); 
				ostr_.put(ownerLoginUser_ );
				break;
			}

			case ADMIN_CMD::EASTFLEX_START_DESKTOP_RESPONSE:
			{
				ostr_.put(eventServerHost_);
				ostr_.put(eventServerPort_);
				break;
			}
			case ADMIN_CMD::EASTFLEX_BLADE_INFO_RESPONSE:
			{
				ostr_.put(ownerId_);		
				ostr_.put(sysInfo_.boardType_);		
				ostr_.put(sysInfo_.removableDev1_);
				ostr_.put(sysInfo_.removableDev1_Status_);
				ostr_.put(sysInfo_.removableDev2_);
				ostr_.put(sysInfo_.removableDev2_Status_);
				ostr_.put(sysInfo_.removableDev3_);
				ostr_.put(sysInfo_.removableDev3_Status_);
				ostr_.put(sysInfo_.npuIP_);
				ostr_.put(sysInfo_.isPowerHouseChassis_); //Sourabh::HotWire::0905
				ostr_.put(sysInfo_.powerHouseChassisID_);
				#ifndef WIN32
				string totalMem		= CommonUtilFuncs::getTotalSystemMemory();
				string availableMem   = CommonUtilFuncs::getAvailableSystemMemory();
				ostr_.put(totalMem);
				ostr_.put(availableMem);
				#endif
				int size=0;
				istr_.get(size);
				ostr_.put(size);
				for(int i=0;i<size;i++)
				{
					string name="",processid="",cpu="",mem="";
					istr_.get(name);
					istr_.get(processid);
					int iprocessid = atoi((char*)processid.c_str());
					#ifndef WIN32
					cpu = CommonUtilFuncs::getCpuUsages(iprocessid);
					mem = CommonUtilFuncs::getMemUsages(iprocessid);
					#endif
					ostr_.put(name);
					ostr_.put(processid);
					ostr_.put(cpu);
					ostr_.put(mem);
				}
				break;
			}
			case ADMIN_CMD::EASTFLEX_HW_INFO_RESPONSE:
			{
					ostr_.put(sysInfo_.localHostName_);
					ostr_.put(sysInfo_.boardType_);
					ostr_.put(sysInfo_.macID_);
					ostr_.put(sysInfo_.bootEth_);
					ostr_.put(sysInfo_.osImage_);
					ostr_.put(sysInfo_.shelfMgr_);
					ostr_.put(sysInfo_.slotNo_);
					ostr_.put(sysInfo_.bootType_);

					ostr_.put(sysInfo_.removableDev1_);
					ostr_.put(sysInfo_.removableDev2_);
					ostr_.put(sysInfo_.removableDev3_);
					ostr_.put(sysInfo_.npuIP_);
					ostr_.put(sysInfo_.eventServerIP_);
					ostr_.put(sysInfo_.hasRTM_);
					result = "Infocollected";
					break;
			}
			case ADMIN_CMD::EASTFLEX_STATUS_EVENT:
			{
				ostr_.put(result);
				break;
			}

		}
		ostr_.put(result);
		char* buff ;int length ;
		ostr_.getBuffer(buff,length);
		IFLOG(logManager_,LoggerManager::Level3)
		{
			string tmp = "Sending Response AdminId = "+admin->adminId_;
			tmp+="::Status = "+status2String(status_);
			logManager_->logString(tmp,LoggerManager::Level1);
		}

		admin->write(buff,length);
		
		return ret ;
}



void AdminListener::sendAdminListenerStatusToDesktop()
{
	if(currentAdmin_ && currentAdmin_->adminId_ == ipRemoteMgntPanel_ )
	{
		IFLOG(logManager_,LoggerManager::Level3)
		{	string temp = "Admin Id is  " + currentAdmin_->adminId_ ;
			temp +=", Reserved IP is " + ipRemoteMgntPanel_;
			temp += ",Traffic Runner has Reserved Admin Listener of same Machine ";
			logManager_->logString(temp,LoggerManager::Level6);
		}
		return;
	}
	string status = "available";
	if(currentAdmin_)
		status = "reserved";
	else
		status = "available";
	Notice  event("admlistener:serverstat");
	OStreamBuf ostr;
	ostr.reset();
	ostr.put(status);// Current Status
	string adminID = "";
	if(currentAdmin_)
		adminID = currentAdmin_->adminId_;
	ostr.put(adminID);
	char* buff=0;
	int length=0;
	ostr.getBuffer(buff,length);
	event.setPayload(buff,length);
	if(producer_)
		producer_->sendEvent_nonblocking(&event);
	IFLOG(logManager_,LoggerManager::Level6)
	{
		string tmp = "Admin Listener Local Desktop Status is :  "+ status ;
		logManager_->logString(tmp,LoggerManager::Level6);
	}

}

void AdminListener::sendAvailableStatusToDesktop()
{

	if(checkEventServerStatus() == EventUser::CONNECTED)
	{
		string status = "available";
		Notice  event("admlistener:serverstat");
		OStreamBuf ostr;
		ostr.reset();
		ostr.put(status);// Current Status
		string admin = "NONE";
		if(currentAdmin_)
			ostr.put( currentAdmin_->adminId_);
		else
			ostr.put(admin);

		char* buff=0;
		int length=0;
		ostr.getBuffer(buff,length);
		event.setPayload(buff,length);
		if(producer_)
			producer_->sendEvent_nonblocking(&event);
	}
}


bool AdminListener::checkLocalSystem(AdminListenerConnectionPort* admin)
{
	bool isEventServerRunning = false;

	if(admin->adminId_ == ipRemoteMgntPanel_ )//No need To Check For Event Server
	{

		IFLOG(logManager_,LoggerManager::Level3)
		{	string temp = "Admin Id is  " + admin->adminId_ ;
			temp +=", Reserved IP is " + temp += ipRemoteMgntPanel_;
			temp += ",Traffic Runner has Reserved Admin Listener of same Machine ";
			logManager_->logString(temp,LoggerManager::Level6);
		}
		return false;
	}

	string error_str;
	string eventserverhost;
	int eventserverport;
	getEventServerHostPort(admin,eventserverhost,eventserverport,error_str);
	
	EventConsumer *consumer = 0x0;
	consumer = new EventConsumer(eventserverport,eventserverhost);

	if( consumer->currState_ ==EventUser::NOT_CONNECTED)
	{
		status_ = ADMIN_STATE::AVAILABLE;
		current_state_ = ADMIN_STATE::AVAILABLE;
	
		string tmp = "Available";
		sendResponse(admin,ADMIN_STATE::AVAILABLE,status_,tmp);
		consumer->reconnect();
	}
	if(consumer->currState_ == EventUser::CONNECTED)
	{
		isEventServerRunning = true;
		status_ = ADMIN_STATE::EVENTSERVER_ALREADY_STARTED;
		string tmp = "Desktop May be Locally Started... ";
		sendResponse(admin,ADMIN_STATE::EVENTSERVER_ALREADY_STARTED,status_,tmp);
		TRACELN(Trace::Debug, tmp);
		IFLOG(logManager_,LoggerManager::Level3)
		{
			logManager_->logString(tmp,LoggerManager::Level1);
		}
		

	}
	 if(consumer)
                delete consumer;
          consumer = 0x0 ;
	
	return isEventServerRunning;
}

void AdminListener::attach(AdminListenerConnectionPort* admin ) 
{
	if(currentAdmin_)
	{
		currentAdmin_ = admin;
		ownerId_ = admin->adminId_;
	}
	else
	{
		string tmp = "No admin to attach";
		sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
		TRACELN(Trace::Debug, tmp);
		IFLOG(logManager_,LoggerManager::Level3)
		logManager_->logString(tmp,LoggerManager::Level1);
	}
}

void AdminListener::bootServer(bool restart)
{
	if(restart)
	{
		#ifndef WIN32
			system("/sbin/init 6");
		#else
			SystemUtils::forceReStart();
		#endif
	}
	else
	{
		#ifndef WIN32
		system("/sbin/init 0");
		#else
			SystemUtils::forceShutDown();
		#endif
	}
}

void AdminListener::reserve(AdminListenerConnectionPort* admin ) 
{
	//SA here there may be a race condition when a notice to shutdown is received and
	//before the Shutdown completes ADMIN_CMD::RESERVE event comes in,
	TRACELN(Trace::Debug, "Received request to reserve...");
	bool status = false;
	if(checkLocalMachine_ && checkLocalSystem(admin))
		return ;
	
	if(!currentAdmin_) //ashok s: if it is not reserved by any admin 
	{
		currentAdmin_ = admin;
		ownerId_  = currentAdmin_->adminId_;
		ownerLoginUser_ = currentAdmin_->loginUser_;
		loadServerName_ = currentAdmin_->serverName_;
		status_ = ADMIN_STATE::RESERVED ;
		current_state_ = ADMIN_STATE::RESERVED ;
		string tmp = "Reserved the server successfully..";
		sendResponse(admin,ADMIN_CMD::RESERVE_RESPONSE,status_,tmp);	
		//sendResponse(admin,RESERVE_RESPONSE,current_state_,tmp);	

		TRACELN(Trace::Debug, tmp);
		IFLOG(logManager_,LoggerManager::Level3)
		{
			logManager_->logString(tmp,LoggerManager::Level1);
		}
	}
	else 
	{
		string tmp = "Server is already reserved by some other Admin , Admin Id = ::";
		tmp += currentAdmin_->adminId_;
		sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
		TRACELN(Trace::Debug, tmp);
		IFLOG(logManager_,LoggerManager::Level3)
		{
			logManager_->logString(tmp,LoggerManager::Level1);
		}
	}
}

void AdminListener::reserve_EastFlex(AdminListenerConnectionPort* admin )
{
	TRACELN(Trace::Debug, "Received request to reserve...");
    bool status = false;
	current_state_ = ADMIN_STATE::RESERVED;
    if(status_ == ADMIN_STATE::AVAILABLE)
		status_ = ADMIN_STATE::RESERVED ;
	
	currentAdmin_ = admin;
	ownerId_  = currentAdmin_->adminId_;
    ownerLoginUser_ = currentAdmin_->loginUser_;
    loadServerName_ = currentAdmin_->serverName_;
	runnerType_ = ADMIN_RUNNER::EastflexRunner;
	if(checkLocalSystem())
	{
		reserveByLocal_ = true;
	}
	if(checkEventServerStatus() == EventUser::CONNECTED)
	{
		status_ = ADMIN_STATE::DESKTOP_STARTED;
		desktopStarted_  = true;
		isDesktopRunning_ = true;
	}
	string tmp = "Reserved the server successfully..";
	sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_RESERVE_RESPONSE,tmp);
}

//This is applicable for EastFLexRunner only
bool AdminListener::checkLocalSystem()
{
	bool ret = false;
	int index = ownerId_.find("@");
	string name = ownerId_.substr(index+1,ownerId_.length());	
	if(loadServerName_ == name)
		ret = true;
	return ret;
}

void AdminListener::unreserve_EastFlex(AdminListenerConnectionPort* admin )
{
	bool bucketCleared = false;
	for(int i=0; i<ipGenReq_.size();i++)
	{
		string ipType = ipGenReq_[i].ipType;
		string startingIP = ipGenReq_[i].startIp;
		string device = ipGenReq_[i].device;
		int maskVal = ipGenReq_[i].maskVal;
		int noOfIPs = ipGenReq_[i].noOfIPs;
		int step = ipGenReq_[i].step;
		string ethType = ipGenReq_[i].ethType;
		string ethPort = "eth1";
		if(stricmp(sysInfo_.boardType_.c_str(),"AT8060") == 0)
			ethPort = "eth5";
		else if (stricmp(sysInfo_.boardType_.c_str(),"TravelHawk") == 0)
			ethPort = "eth2";
		else
			ethPort = "eth1";
		string ethdev = device;
		int deviceIndex = 0;

//		long long starttime = CommonUtilFuncs::getSysTimeInMilli();
//		for (int i=1; i<=noOfIPs; i++)
//		{  
			if(stricmp(ethType.c_str(),"NPU")==0)
			{
				deviceIndex = getDeviceIndex(ethPort);
				ethdev = ethPort;
			}
			else
			{
				deviceIndex = getDeviceIndex(device);
			}
			if(ipType == "IPv4")
			{
				if(bucketCleared == false)
				{
					clearIPBucketSecondary(admin,ethdev,maskVal,4);
					bucketCleared = true;
				}
				addOrDeleteRouteIPV4(startingIP,maskVal,ethdev,false);
			}
			else if(ipType == "IPv6")
			{ //Sourabh::110918::Support of IPv6 Removal
				if(netLinkFd_>=0)
				{
					close(netLinkFd_);
					netLinkFd_=-1;
				}
				clearIPBucket(ethdev,maskVal,6);
				NetLinkUtils::closeFD();
			}
			else
			{
				if(stricmp(ethType.c_str(),"NPU")==0)
				{
					addOrDeleteIpv6ToHost(startingIP,ethPort,maskVal,false);
				}
				else
				{
					addOrDeleteIpv6ToHost(startingIP,device,maskVal,false);

				}
				//addOrDeleteRouteIPV6();
			}
	}
	string tmp = "Successfully cleared IP Addresses...";
	sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_STATUS_EVENT,tmp);
	clearIPBuckets();//Clear All IP Buckets.
	if(netLinkFd_>=0)
	{
		close(netLinkFd_);
		netLinkFd_=-1;
	}
	ipGenReq_.clear();
	current_state_ = ADMIN_STATE::AVAILABLE;
	status_ = ADMIN_STATE::AVAILABLE;
	currentAdmin_ = 0;
	ownerId_  = "NONE";
	ownerLoginUser_ = "";
	loadServerName_ = "";
	runnerType_ = -1;
	desktopStarted_  = false;
	isDesktopRunning_ = false;
    tmp = "unreserved the server successfully..";
	sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_UNRESERVE_RESPONSE,tmp);
	sendAdminListenerStatusToDesktop(); //reset the Desktop reserver flag
}

void AdminListener::sendStatus(AdminListenerConnectionPort* admin ) 
{
		ostr_.reset();
		ostr_.put(version_);
		ostr_.put(ADMIN_CMD::STATUS_EVENT);
		ostr_.put(status_);
		string owner="";
		if(currentAdmin_ && !currentAdmin_->isAlive_)
			owner = ownerId_+"[Disconnected]";
		else
			owner = ownerId_;
		ostr_.put(owner);
		ostr_.put(ownerLoginUser_);
		if(status_ == ADMIN_STATE::DESKTOP_ALREADY_STARTED || 
			status_ == ADMIN_STATE::DESKTOP_STARTED)
		{
			ostr_.put(eventServerHost_);
			ostr_.put(eventServerPort_);
		}
		ostr_.put(netInterfaces_);
		string tmp="";
		IFLOG(logManager_,LoggerManager::Level3)
		{
			tmp = "Sending Status Message AdminId = "+admin->adminId_;
			tmp+="::Status = "+status2String(status_);
			logManager_->logString(tmp,LoggerManager::Level1);
		}
		tmp="";
		ostr_.put(tmp);
		char* buff ;int length ;
		ostr_.getBuffer(buff,length);
		admin->write(buff,length);
}

//Ashok s: this method is called to start the desktop .

void AdminListener::startDesktop(AdminListenerConnectionPort* admin ) 
{
	int result = checkEventServerStatus();
	//if the eventserver is connected, check the desktop status; we should be getting
	//status every 10 secs (based on timer);
	if(result )
	{
		status_ = ADMIN_STATE::STARTDESKTOP_INPROGRESS;
	}
	else 
	{
		timerMgr_->stopTimer(desktopStatusTimer_);
	        invokeDesktop(admin);
		
		if(checkEventServerStatus() == EventUser::CONNECTED)
		{
			Notice  event("DesktopStatusRequest");
			producer_->sendEvent_nonblocking(&event);
		}
	}
	//If desktop is not started, then start the desktop;
}

void AdminListener::startDesktopEastFlex(AdminListenerConnectionPort* admin ) 
{
	if(status_ == ADMIN_STATE::RESERVED || status_ == ADMIN_STATE::DESKTOP_STOPPED)
	{
		status_ = ADMIN_STATE::STARTDESKTOP_INPROGRESS;
		string tmp = "Invoking EAST Desktop....";
		sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_STATUS_EVENT,tmp);

		timerMgr_->stopTimer(desktopStatusTimer_);
		invokeEastFlexDesktop(admin);
		if(checkEventServerStatus() == EventUser::CONNECTED)
		{
			Notice  event("DesktopStatusRequest");
			producer_->sendEvent_nonblocking(&event);
		}
	}
	else if(status_ == ADMIN_STATE::DESKTOP_STARTED)
	{
		string tmp = "EAST Desktop is already started....";
		sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_STATUS_EVENT,tmp);
	}
}

void AdminListener::invokeDesktop(AdminListenerConnectionPort* admin)
{
	string error_string;
	TRACELN(Trace::Debug, "Starting Desktop...");
	
	int ret = 1;
	if(admin->osCommandList_.size() == 1)
	{
		ret = CommonUtilFuncs::executeOSCommand(admin->osCommandList_,error_string); // execute the os commands 
	}
	if(ret != -1)
	{
		ret = createProcess(admin,error_string);
	}
	mySleep(2); // sleep a second for everything to start
	if(ret )
	{
		if(consumer_ )
		{
			delete consumer_;
			consumer_ = 0x0 ;
		}
		if (producer_)
		{
			delete producer_;
			producer_ = 0x0 ;
		}
		producer_ = new EventProducer(eventServerPort_,eventServerHost_);
		consumer_ = new EventConsumer(eventServerPort_,eventServerHost_);

		int cnt = 0;
		if(ret)
		{
			while(!consumer_->isConnected())
			{
				cnt++;
				if(cnt >=20 )
				{
					status_ = ADMIN_STATE::DESKTOP_NOT_STARTED;
					error_string = "started the desktop .unable to connect eventserver  ";
					ret = 0;
					break;
				}
				#ifdef WIN32
				  Sleep(1*1000);
				#else
				  sleep(1);
				#endif	
			}
		}
		if(ret)
			registerEvents();
	}
	if(ret)
	{
		if(checkEventServerStatus() == EventUser::CONNECTED)
		{
			Notice  event("DesktopStatusRequest");
			producer_->sendEvent_nonblocking(&event);
		}
	    status_ = ADMIN_STATE::DESKTOP_STARTED;
		desktopStarted_  = true;
		isDesktopRunning_ = true;
		loadServerName_ = admin->serverName_;
		sendResponse(admin,ADMIN_CMD::START_DESKTOP_RESPONSE,status_,"Successfully invoked the desktop ");
		TRACELN(Trace::Debug, "Desktop successfully invoked.");
		IFLOG(logManager_,LoggerManager::Level3)
		{
			string tmp = "Desktop successfully invoked........";
			logManager_->logString(tmp,LoggerManager::Level3);
		}
	}
	else
	{
		status_ = ADMIN_STATE::DESKTOP_NOT_STARTED;
		string tmp = "Failed to start the desktop . error = "+ error_string;
		sendResponse(admin,ADMIN_CMD::FAILURE,status_,"Unable to invoke desktop , error = "+error_string);
		TRACELN(Trace::Debug, tmp);
		IFLOG(logManager_,LoggerManager::Level3)
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	timerMgr_->startTimer(desktopStatusTimer_);

	timerMgr_->startTimer(restDesktopStatusTimer_);
	
}

void AdminListener::invokeEastFlexDesktop(AdminListenerConnectionPort* admin)
{
	string error_string;
	TRACELN(Trace::Debug, "Starting Desktop...");
	
	int ret = 1;
	if(reserveByLocal_)
	{
		getEventServerHostPort(admin,eventServerHost_,eventServerPort_,error_string);
	}

	if(environment_.getOSCommands().size() > 0)
	{
		ret = CommonUtilFuncs::executeOSCommand(environment_.osCommands_,error_string);
		IFLOG(logManager_,LoggerManager::Level3)
		{
			int size = environment_.getOSCommands().size();
			string tempstr= "Number of OS command size ::"+CommonUtilFuncs::itoa(size);
			logManager_->logString(tempstr,LoggerManager::Level3);
			for(int i = 0; i<size; i++)
			{
				tempstr= "Executed OS command ::";	
				tempstr+=environment_.getOSCommands()[i];
				logManager_->logString(tempstr,LoggerManager::Level3);
			}
		}
	}
	// Fixed for Remote blade issue.createProcess method should allways executed for setting the paths in remote machine. 
	//else
	//{
		ret = createProcess(admin,error_string);
	//}
	
	mySleep(2); 
	if(ret )
	{
		if(consumer_ )
		{
			delete consumer_;
			consumer_ = 0x0 ;
		}
		if (producer_)
		{
			delete producer_;
			producer_ = 0x0 ;
		}
		producer_ = new EventProducer(eventServerPort_,eventServerHost_);
		consumer_ = new EventConsumer(eventServerPort_,eventServerHost_);

		int cnt = 0;
		if(ret)
		{
			while(!consumer_->isConnected())
			{
				cnt++;
				if(cnt >=10 )
				{
					status_ = ADMIN_STATE::DESKTOP_NOT_STARTED;
					error_string = "started the desktop .unable to connect eventserver  ";
					ret = 0;
					break;
				}
				#ifdef WIN32
				  Sleep(2*1000);
				#else
				  sleep(2);
				#endif	
			}
		}
		if(ret)
			registerEvents();
	}
	if(ret)
	{
		if(checkEventServerStatus() == EventUser::CONNECTED)
		{
			Notice  event("DesktopStatusRequest");
			producer_->sendEvent_nonblocking(&event);
		}
		
	    desktopStarted_  = true;
		isDesktopRunning_ = true;
		loadServerName_ = admin->serverName_;
		status_ = ADMIN_STATE::DESKTOP_STARTED;
		//Chinmaya
		sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_START_DESKTOP_RESPONSE,"Successfully invoked the EAST Desktop..");
		
		TRACELN(Trace::Debug, "Desktop successfully invoked.");
		IFLOG(logManager_,LoggerManager::Level3)
		{
			string tmp = "Desktop successfully invoked........";
			logManager_->logString(tmp,LoggerManager::Level3);
		}
	}
	else
	{
		status_ = ADMIN_STATE::DESKTOP_NOT_STARTED;
		current_state_ = ADMIN_STATE::RESERVED;
		string tmp = "Failed to start the desktop . error = "+ error_string;
		sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_ERROR,"Unable to invoke desktop , error = "+error_string);
		TRACELN(Trace::Debug, tmp);
		IFLOG(logManager_,LoggerManager::Level3)
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	timerMgr_->startTimer(desktopStatusTimer_);
}

string AdminListener::status2String(int status)
{
		string result ="";
		switch(status)
		{
			case ADMIN_STATE::AVAILABLE:
				result = "AVAILABLE";
				break ;
			case ADMIN_STATE::RESERVED:
				result = "RESERVED";
				break ;
			case ADMIN_STATE::DESKTOP_NOT_STARTED :
				result = "DESKTOP_NOT_STARTED";
				break ;
			case ADMIN_STATE::DESKTOP_STOPPED:
				result = "DESKTOP_STOPPED";
				break ;
			case ADMIN_STATE::DESKTOP_STARTED:
				result = "DESKTOP_STARTED";
				break ;
			case ADMIN_STATE::DESKTOP_ALREADY_STARTED :
				result = "DESKTOP_ALREADY_STARTED";
				break ;
			case ADMIN_STATE::STOPDESKTOP_INPROGRESS:
				result = "STOPDESKTOP_INPROGRESS";
				break ;
			case ADMIN_STATE::UNRESERVE_INPROGRESS:
				result = "UNRESERVE_INPROGRESS";
				break ;
			case ADMIN_STATE::STARTDESKTOP_INPROGRESS:
				result = "STARTDESKTOP_INPROGRESS";
				break ;
				break ;
			case ADMIN_STATE::EVENTSERVER_ALREADY_STARTED:
				result = "EVENTSERVER_ALREADY_STARTED";
				break ;

		}
		return result ;
}

void AdminListener::sendStopDesktopRequest()
{
	Notice  event("");
	event.name("loadprofile::exitdesktop");
	string loadServName = "";
	loadServName = getLoadServerName();
	event.setProperty("loadServerName",loadServName);
	producer_->sendEvent_nonblocking(&event);
	desktopStarted_=false; 
	timerMgr_->startTimer(stopDesktoptimer_);
}
	

//Ashok s: this method is called to stop the desktop .
void AdminListener::stopDesktop(AdminListenerConnectionPort* admin)
{ 
	//SA we need to change the status to ShutingDown so that
	//Further requests can be notified that Server is in Shutdown mode, 
	//so that nothing corrupts
	 if (desktopStarted_)
	 {
		TRACELN(Trace::Debug, "Sending notice to stop desktop.");
		IFLOG(logManager_,LoggerManager::Level3)
		{
			string tmp = "Sending notice to stop desktop.";
			logManager_->logString(tmp,LoggerManager::Level3);
		}	
		sendStopDesktopRequest();
		status_ = ADMIN_STATE::STOPDESKTOP_INPROGRESS;
		sendResponse(admin,ADMIN_CMD::STOP_DESKTOP_RESPONSE,status_,"Trying to stop desktop.");
		TRACELN(Trace::Debug, "stopping the desktop.");
		IFLOG(logManager_,LoggerManager::Level3)
		{
			string tmp = "stopping the desktop........";
			logManager_->logString(tmp,LoggerManager::Level3);
		}
		isDesktopRunning_ =false;
	}

}

int AdminListener::startRunner(string& port)
{
	Notice  event("");
	event.name("eastflex::start");
	event.setProperty("restport",port);
	producer_->sendEvent_nonblocking(&event);
}

int AdminListener::stopRunner()
{
	Notice  event("");
	event.name("eastflex::exit");
	string loadServName = "";
	loadServName = getLoadServerName();
	event.setProperty("loadServerName",loadServName);
	producer_->sendEvent_nonblocking(&event);
}

//Ashok s: this method is called to unreserve the desktop .
void AdminListener::unreserve(AdminListenerConnectionPort* admin)
{
    sendAvailableStatusToDesktop();
	//SA I guess u need to shutdown process.
	TRACELN(Trace::Debug, "Received request to unreserve...");
	IFLOG(logManager_,LoggerManager::Level3)
	{
		string tmp = "Received request to unreserve...";
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	//Ashok A, we need to make sure we send out a request to bring the
	//desktop down??
	string tmp ;
	if (desktopStarted_)
	{
		status_ = ADMIN_STATE::UNRESERVE_INPROGRESS ;
		tmp = "Unreserve inprogress, stopping the desktop first ...";
		stopDesktop(admin);
		isDesktopRunning_ =false;
	}
	status_ =  ADMIN_STATE::AVAILABLE;

	ownerId_="NONE";
	ownerLoginUser_ ="";
	loadServerName_ = "";
	tmp = "Successfully unreserve the server.";
	currentAdmin_ = 0; //remove the current admin
	//Ashok A	send request to every connection that the machine
	//is now available.
	vector<NetworkPort*>::iterator iter;
	iter = networkports_.begin();
	while(iter != networkports_.end())
	{
		AdminListenerConnectionPort *port= (AdminListenerConnectionPort*)*iter;
		//sendResponse(port,UNRESERVE_RESPONSE,status_,tmp);
		sendResponse(port,ADMIN_CMD::UNRESERVE_RESPONSE,current_state_,tmp);		//SK::06SEPT
		++iter;
	}
	TRACELN(Trace::Debug, "Unreserved...");
	IFLOG(logManager_,LoggerManager::Level3)
	{
		string tmp = "Unreserved......";
		logManager_->logString(tmp,LoggerManager::Level3);
	}
}


//SA  Read is not a good function, because this one accepts so something similar
//Ashok s: as we derive this from NetworkTCPPort so for that the name is read .
void AdminListener::read(char* &data, int& length) 
{
	#ifdef WIN32
		int
	#else
		socklen_t
	#endif
	size = sizeof( struct sockaddr_in);
	struct sockaddr_in to;
	int fd = accept(fd_,(struct sockaddr*)&to,&size);
	fcntl(fd, F_SETFD, FD_CLOEXEC);//Sourabh::27082014
	//
	struct linger ling;
	ling.l_onoff = 0;
	ling.l_linger = 0;
	setsockopt(fd,SOL_SOCKET,SO_LINGER,(const char*)&ling,sizeof(struct linger));

	//
	length = 0;
	AdminListenerConnectionPort* port = 0;
	int objectIndex = ObjectPool::getObjectIndex("AdminListenerConnectionPort");
	port = (AdminListenerConnectionPort*)ObjectPool::getObjectFromPool(objectIndex);
	if (port == 0) 
	{
		port  = new AdminListenerConnectionPort();
		ObjectPool::usedMap_[port]=objectIndex ;
		port->objectIndex_ = objectIndex ;
	}
	port->currState_ = ACTIVE;
	port->logManager_ =   logManager_  ;
	addConnectionPort(port, fd, (void *) &to);
	IFLOG(logManager_,LoggerManager::Level1)
	{
		string tmp = "Connection accepted . ";
		in_addr from_host;
		int from_port = to.sin_port;
		from_port = ntohs (from_port) ;
		from_host = (in_addr)to.sin_addr;
		std:: string host= inet_ntoa(from_host); 
		tmp += host;
		logManager_->logString(tmp,LoggerManager::Level1);
	}
}


int AdminListener:: waitOnSelect(int timetoWait)
{
	int result  = 0;
	while (1)
	{
		int fds = 0;
		fd_set read,write,errfd;
		FD_ZERO(&read);
		FD_ZERO(&write);
		FD_ZERO(&errfd);
		setPortReadFDs(read,fds);
		setPortWriteFDs(write,fds);
		setPortReadFDs(errfd,fds);
		setEventFd(read,fds);
  		fds++;
	    	timeval time;
		time.tv_sec = 0;
		time.tv_usec = 5*1000;
    		result = select(fds,&read,&write,&errfd,&time);
		if ( result == -1)
		{
			clearErrorFDs(errfd,1);//Sourabh::27082014 ::Discussed with Kamal
			//Sourabh::1604 Added Trace
			switch(errno)
			{
			case EBADF:TRACELN(Trace::Debug, "BAD File Descriptor...Perhaps a file descriptor that was already closed, or one on which an error has occurred");
				break;
			case ENOMEM: TRACELN(Trace::Debug,"Unable to allocate memory for internal tables");
				break;
			}
			clearErrorFDs(errfd,1);//Sourabh/Kamal::26082014
		}
		else if ( result == 0 )
			handleTimeout();
		else
		{
			clearErrorFDs(errfd,fds);	 //Sourabh::14062016::allow read the full fd array count::EAST-26006
			readPortFD(read,fds);		
			readEventFDs(read);
		}
	}
	return 1;
}
void AdminListener::clearErrorFDs(fd_set& errfds,int maxfds)
{
	int count = TCPUtilities::getFDs(errfds, fd_list_, maxfds );
	IFLOG(logManager_,LoggerManager::Level3)
	{
		string traceStr = "clearErrorFDs...Count = "+CommonUtilFuncs::itoa(count)+"...maxfds = "+CommonUtilFuncs::itoa(maxfds)+"!!";
		logManager_->logString(traceStr,LoggerManager::Level3);
	}
	for (int i = 0; i < count; i++)
	{
		NetworkPort* port = NetworkPort::fd2Port_[fd_list_[i]];
		if (port)
			if(FD_ISSET(port->fd_,&errfds))
			{
				FD_CLR(port->fd_,&errfds);
				port->stop();
			}
	}
}

void AdminListener::setEventFd(fd_set& read,int &fds)
{
	if (consumer_ && (consumer_->fd() >= 0) )
	{
	  FD_SET(consumer_->fd() ,&read);
	  fds = max(fds,consumer_->fd()  );
	}
}

void AdminListener::handleProcessID(Notice* notice)
{
	istr_.reset();
	istr_.setBuffer((char*)notice->payload(),notice->length());
	int size ;
	istr_.get(size);
	for( int i = 0; i < size ; i++ )
	{
		int id = 	processIdList_[i];
		processIdList_.push_back(id); 
	}
}

void AdminListener::processEvent(Notice* notice)
{
	IFLOG(logManager_,LoggerManager::Level6)
	{
		string tmp = "Received event from EventServer ..."+notice->name();
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	if(notice->name() == "ProcessIdStatus")
	{
		handleProcessID(notice);
	}
	else if(notice->name() == "DesktopStatusResponse")
	{
		string loadServerName = "";
		loadServerName = notice->getProperty("loadServerName");
		desktopPath_ = notice->getProperty("loadServerPath");
		string loadServerNamePresent = getLoadServerName();
		if(runnerType_ == ADMIN_RUNNER::EastflexRunner)
			isDesktopRunning_ = true ;
		//Chinmaya
		if(currentAdmin_ && stricmp((char*)loadServerName.c_str(),(char*)loadServerNamePresent.c_str()) == 0)
			isDesktopRunning_ = true ;
	}
	else if(notice->name() == "east.rm.startle")
	{
		handleStartApplication(notice);
	}
	else if(notice->name() == "east.rm.startsoftgenserver")
	{
		handleStartApplication(notice, false);
#if 0
		handleStartSoftDataGenApplication(notice);
#endif
	}
	else if(notice->name() == "east.rm.stople")
	{
		handleStopApplication(notice);
	}
	else if(notice->name() == "east.rm.stopsoftgenserver")
	{
		handleStopApplication(notice, false);
	}
}

void AdminListener::readEventFDs(fd_set& read) 
{
    Notice* notice = 0 ;

	if ( consumer_ && checkEventServerStatus() == EventUser::CONNECTED)
		notice = consumer_->receiveEvent_nonBlocking(read);
	if ( notice == 0x0 )
	{
		if(checkEventServerStatus() == EventUser::NOT_CONNECTED)
			reconnect();
		if(checkEventServerStatus() == EventUser::CONNECTED)
			registerEvents();
	}
    else 
	{
		processEvent(notice);
		delete notice;
	}
}

// This method is called to check the timeouts .
int AdminListener::handleTimeout()
{
	int ret = 1;
	vector<RunningTimer*> *timerlist;
	if(timerMgr_->getTimeouts(timerlist))
	{
		vector<RunningTimer*>::iterator iter;
		iter=timerlist->begin();
		while (iter !=timerlist->end())
		{
			RunningTimer* t = *iter;
			handleTimeout(t);
			++iter;
		}
		timerlist->clear();
	}
	return ret ;
}

void AdminListener::handleTimeout(RunningTimer* rt )
{
	switch(rt->type_)
	{
		case ADMIN_TIMER::STOP_DESKTOP_TIMER:
		{
			checkAndKillProcess();
			if(status_ ==  ADMIN_STATE::UNRESERVE_INPROGRESS)
			{
				status_ =  ADMIN_STATE::AVAILABLE;
				ownerId_="NONE";
				vector<NetworkPort*>::iterator iter;
				iter = networkports_.begin();
				string tmp = "Successfully unreserve the server.";
				while(iter != networkports_.end())
				{
					AdminListenerConnectionPort *port= (AdminListenerConnectionPort*)*iter;
					sendResponse(port,ADMIN_CMD::UNRESERVE_RESPONSE,status_,tmp);
					++iter;
				}
				currentAdmin_ = 0; //remove the current admin
			}
			else if(status_ ==  ADMIN_STATE::AVAILABLE)
				;
			else
			{
				status_ = ADMIN_STATE::DESKTOP_STOPPED ;
				desktopStarted_=false; 
				if(currentAdmin_)
					sendResponse(currentAdmin_,ADMIN_CMD::STOP_DESKTOP_RESPONSE,status_,"Successfully stoped desktop .");
			}
			break;
		}
		case ADMIN_TIMER::STATUS_TIMER:
		{
			int size = networkports_.size();
			for(int i = 0 ; i < size ;i++)
			{
				AdminListenerConnectionPort *port= (AdminListenerConnectionPort*)networkports_[i];
				sendStatus(port);
			}
			timerMgr_->startTimer(statusTimer_);
			break;	
		}
		case ADMIN_TIMER::KEEP_ALIVE_TIMER:
		{
			int size = networkports_.size();
			for(int i = 0 ; i < size ;i++)
			{
				AdminListenerConnectionPort *port= (AdminListenerConnectionPort*)networkports_[i];
				if(!port->isAlive_  &&  (-1 == port->fd_)  )
				{
					port->stop();
					if(runnerType_ == ADMIN_RUNNER::EastflexRunner && currentAdmin_ == port) 
						current_state_ = ADMIN_STATE::AVAILABLE;
					IFLOG(logManager_,LoggerManager::Level1)
					{	
						string tmp = "Connection reset . Unable to receive KEEP_ALLIVE Notice. Port->stop() called";
						logManager_->logString(tmp,LoggerManager::Level1);
					}	
				}
				else
				{
					ostr_.reset();
					ostr_.put(version_);
					ostr_.put(ADMIN_CMD::KEEP_ALIVE);
					ostr_.put(status_);
					char* buff ;int length ;
					ostr_.getBuffer(buff,length);
					port->write(buff,length);
					port->isAlive_  = false ;
				}
			}
			timerMgr_->startTimer(KeepAliveTimer_);
			break;
		}
		case ADMIN_TIMER::RECONNECT_TIMER:
		{
			reconnect();
			timerMgr_->startTimer(reconnectTimer_ );
			break;
		}
		case ADMIN_TIMER::DESKTOP_STATUS_TIMER:
		{
			if(isDesktopRunning_ == true )
			{
				status_ = ADMIN_STATE::DESKTOP_STARTED;
				desktopStarted_  = true ;
			}
			else
			{
				if(status_ == ADMIN_STATE::DESKTOP_ALREADY_STARTED || 
				status_ == ADMIN_STATE::DESKTOP_STARTED)
				status_= ADMIN_STATE::DESKTOP_NOT_STARTED;
				desktopStarted_  = false ;
			}
			if(status_ == ADMIN_STATE::STARTDESKTOP_INPROGRESS )
			{
				if(currentAdmin_)
				{
					if(runnerType_ == ADMIN_RUNNER::EastflexRunner)
						invokeEastFlexDesktop(currentAdmin_ ); 
					else
						invokeDesktop(currentAdmin_ ); 
				}
				
			}
			if(checkEventServerStatus() == EventUser::CONNECTED)
			{
				Notice  event("DesktopStatusRequest");
				producer_->sendEvent_nonblocking(&event);

			}
			timerMgr_->startTimer(desktopStatusTimer_);
			break;	
		}
		case ADMIN_TIMER::REST_DESKTOP_STATUS_TIMER:
		{
			if(isDesktopRunning_)
			{
				/*ostr_.reset();
				string status="desktop::started";
				ostr_.put(status);
				char* buff ;int length ;
				ostr_.getBuffer(buff,length);
				currentAdmin_->write(buff,length);*/
				startRunner(restPort);
			} else {
				/*
				ostr_.reset();
				string status="desktop::notstarted";
				ostr_.put(status);
				char* buff ;int length ;
				ostr_.getBuffer(buff,length);
				currentAdmin_->write(buff,length);
				*/
			}
			break;
		}
		case ADMIN_TIMER::SERVER_STATUS_TIMER:
		{
				break;
		}
		case ADMIN_TIMER::ATR_STATUS_TIMER:
		{
			 if(checkEventServerStatus() == EventUser::CONNECTED)
             {
				sendAdminListenerStatusToDesktop();	
			 }

			if(checkEventServerStatus() == EventUser::NOT_CONNECTED) 
            {
                 if(status_ == ADMIN_STATE::EVENTSERVER_ALREADY_STARTED)
                    status_= ADMIN_STATE::AVAILABLE;
            }

			timerMgr_->startTimer(atrStatusTimer_);
			break;
		}
	}
}

int AdminListener::checkEventServerStatus()
{
	return consumer_->currState_;
}

void AdminListener::registerEvents()
{
	string tmp = "DesktopStatusResponse" ;
	consumer_->waitFor(tmp);
	tmp = "ProcessIdStatus";
	consumer_->waitFor(tmp);
	
	tmp = "east.rm.startle";
	consumer_->waitFor(tmp);
	tmp = "east.rm.startsoftgenserver";
	consumer_->waitFor(tmp);
	tmp = "east.rm.stopsoftgenserver";
	consumer_->waitFor(tmp);
	tmp = "east.rm.stople";
	consumer_->waitFor(tmp);
}

void  AdminListener::reconnect()
{
	if(checkEventServerStatus() == EventUser::NOT_CONNECTED)
	{
		consumer_->reconnect();
		producer_->reconnect();
		if(checkEventServerStatus() == EventUser::CONNECTED)
			registerEvents();
	}
	if(checkEventServerStatus() == EventUser::CONNECTED)
	{
		
	}
	else
	{
		if(status_ == ADMIN_STATE::DESKTOP_ALREADY_STARTED || 
		status_ == ADMIN_STATE::DESKTOP_STARTED)
		{
			status_= ADMIN_STATE::DESKTOP_STOPPED;
			desktopStarted_ = false ;
			isDesktopRunning_ = false;
		}
	}
}

//This method check if the process running , if so kill the process .
int AdminListener::checkAndKillProcess()
{
	int ret = 1;
	//if any of the processId is still alive, kill them();
	//Ashok s:  i will add the code to kill the process .
	processIdList_.clear();
	return ret ;
}

int AdminListener::sendResponse(AdminListenerConnectionPort* admin , int command , int status, string result)
{
		int ret = 1;
		ostr_.reset();
		ostr_.put(version_);
		ostr_.put(command);
		ostr_.put(status);
		switch(command)
		{
		case ADMIN_CMD::RESERVE_RESPONSE:
			{
				ostr_.put(ownerId_);
				ostr_.put(ownerLoginUser_);
				if (netInterfaces_ == "")
					getNetworkInterfaces(netInterfaces_);
				ostr_.put(netInterfaces_);
				break;
			}
		case ADMIN_CMD::UNRESERVE_RESPONSE:
			{
				ostr_.put(ownerId_); 
				ostr_.put(ownerLoginUser_ );
				break;
			}

			case ADMIN_CMD::START_DESKTOP_RESPONSE:
			{
				ostr_.put(eventServerHost_);
				ostr_.put(eventServerPort_);
				break;
			}
		}
		ostr_.put(result);
		char* buff ;int length ;
		ostr_.getBuffer(buff,length);
		IFLOG(logManager_,LoggerManager::Level3)
		{
			string tmp = "Sending Response AdminId = "+admin->adminId_;
			tmp+="::Status = "+status2String(status_);
			logManager_->logString(tmp,LoggerManager::Level1);
		}

		admin->write(buff,length);
		
		return ret ;
}



//This method is invoke the desktop in the remote after successfully mount network drive
//Ashok A: This method does not belong to this object
//Ashok A: Need to move to appropriate place
//Ashok s: I will move the same to AdminListener .
int AdminListener::createProcess(AdminListenerConnectionPort* admin , string& error_string)
{
  int result;
  char* prg;
  char* dir;
  string tmp ; 
  getEventServerHostPort(admin,eventServerHost_,eventServerPort_,error_string);
	IFLOG(logManager_,LoggerManager::Level3)
	{
		int size = environment_.getOSCommands().size();
		string tempstr= "Number of OS command size ::"+CommonUtilFuncs::itoa(size);
		logManager_->logString(tempstr,LoggerManager::Level3);
	}

  if(environment_.getOSCommands().size() > 0)
	{
		
		int size = environment_.getOSCommands().size();
		for(int i = 0 ; i < size ; i++)
		{
			string command = environment_.getOSCommands()[i];
			if(command.length())
			{
				int ret = system(command.c_str());
				if (ret == -1)
				{
					IFLOG(logManager_,LoggerManager::Level3)
					{
						string result1 ="Failed to execute the os command =  ";
						result1 += command +"::result1 = " + CommonUtilFuncs::itoa(ret) + "\n";
						logManager_->logString(result1 ,LoggerManager::Level3);
					}	
				}
			}
		}
		IFLOG(logManager_,LoggerManager::Level3)
		{
			int size = environment_.getOSCommands().size();
			string tempstr= "Number of OS command size ::"+CommonUtilFuncs::itoa(size);
			logManager_->logString(tempstr,LoggerManager::Level3);
			for(int i = 0; i<size; i++)
			{
				tempstr= "Executed OS command ::";	
				tempstr+=environment_.getOSCommands()[i];
				logManager_->logString(tempstr,LoggerManager::Level3);
			}
		}
	}

  if(!invokedesktop_)
  {
	string mode = "-s";
	if(clientMode_)
		mode = "-c";
	tmp = "java -ms32m  -mx512m  GenericDesktopHandler "+mode;
	prg = (char*)tmp.c_str(); 
  }
  else
	prg = environment_.getCommandLine();
	string temp = prg;
	string prgWithAdminName = temp + " -loadservername " + admin->serverName_;
	prgWithAdminName += " -invokedbyadminListener";
	prgWithAdminName += " -reservedby ";
	prgWithAdminName += admin->adminId_;
	prgWithAdminName += " -nologin";
	
	prg = (char*)prgWithAdminName.c_str();


  IFLOG(logManager_,LoggerManager::Level3)
  {
		string tmp = "Command Line  = ";
		tmp+=prg;
		logManager_->logString(tmp,LoggerManager::Level3);
  }

  dir = environment_.getWorkingDirectory();
  IFLOG(logManager_,LoggerManager::Level3)
  {
		string tmp = "Working Directory   = ";
		tmp+=dir;
		logManager_->logString(tmp,LoggerManager::Level3);
  }
  
#ifdef WIN32
  result = CommonUtilFuncs::createProcess((char*)prg ,dir);
#else
   string program = prg;
   vector<string> argument;
   StringTokenizer  st(program);
   st.PascalComments (false);
   st.WordChars (0x21, 0x7e);
   st.WhiteSpaceChars (',', ',');
   char* args[]={0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x00,0x00};
   st.NextToken();
   string appName = st.GetStrValue();
   argument.push_back(appName);
   string arg ="";
   while(true)
   {
	  st.NextToken();
	  if(st.getType () == TTSTR_EOF)
		      break;
	  arg = st.GetStrValue();
	  argument.push_back(arg);	     
   }
   signal(SIGCLD,SIG_IGN);
   pid_t pid;
   pid = vfork();
   if(pid==0)
   {
	    int size = argument.size();
	    for(int j=0;j<size;j++)
		args[j]=(char*)argument[j].c_str();
		result = execvp(args[0],args);
   }
   else if(pid > 0)
          result = pid;  //The pid of child process
   else
	   ;
   if(result != -1)
	  result = 1;
   else
	  result =0;
#endif
  if(!result)
  {
	char*  name = "JAVAHOME";
	#ifdef WIN32
		const int size = 1024;
		char buffer[size];
		int length = GetEnvironmentVariable(name,buffer,size);
	#else
		char* buffer;
		buffer = getenv(name) ;
	#endif	
	error_string +="Unable to start process ::" ;
	error_string +="for JAVAHOME::"+string(buffer) ;
	error_string +=" ::Process ::" ;
	error_string +=prg ;
	error_string +=" ::Current dir ::";
	error_string += dir ;
	error_string +=" ::Please check the properties and 2.1.1.ini file. ";
	string tmp = "error: ";
	#ifdef WIN32
		tmp += strerror(GetLastError());
	#else
		tmp += strerror(errno);
	#endif
	error_string +=tmp;
  }
  return result ;
}

void AdminListener::readPortFD(fd_set& read, int maxfds ) 
{
	int count = TCPUtilities::getFDs(read, fd_list_, maxfds );
	for (int i = 0; i < count; i++)
	{
		NetworkPort* port = NetworkPort::fd2Port_[fd_list_[i]];
		if (port)
		port->readPortFDs(read);
	}
}


void AdminListener::connectEventServer()
{
	TRACELN(Trace::Debug, "Connecting EventServer...");
	eventServerHost_= Environment::getProperty("EventServerHost");
	string serverport = Environment::getProperty("EventServerPort");
	eventServerPort_ = atoi((const char*)serverport.c_str());
	if(!consumer_)
		consumer_ = new EventConsumer(eventServerPort_,eventServerHost_);
	if(!producer_)
		producer_ = new EventProducer(eventServerPort_,eventServerHost_);
	if(checkEventServerStatus()==EventUser::NOT_CONNECTED)
		reconnect();
	string event = "loadprofile::loadapplication";
	consumer_->waitFor(event);
	event = "loadprofile::destroy";
	consumer_->waitFor(event);
	TRACELN(Trace::Debug, "EventServer Connected.");
}

string AdminListener::request2String(int request)
{
	string result ;
	switch(request)
	{
		case ADMIN_CMD::START_DESKTOP:
		result = "START_DESKTOP";
		break;
		case  ADMIN_CMD::STOP_DESKTOP:
		result = "STOP_DESKTOP";
		break;
		case ADMIN_CMD::UNRESERVE:
		result = "UNRESERVE";
		break;
		case ADMIN_CMD::RESERVE:
		result = "RESERVE";
		break;
		case ADMIN_CMD::KEEP_ALIVE :
		result = "KEEP_ALIVE ";
		break;
		case ADMIN_CMD::RESTART :
		result = "RESTART ";
		break;
		case ADMIN_CMD::SHUTDOWN :
		result = "SHUTDOWN";
		break;
	}
	return result ;
}
int  AdminListener::openFD()
{
    int fd;
	//fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if(fd <=0)
		return -1;
	return fd;
}
void AdminListener::addOrDeleteIpv4ToHost(const char* startingIP, int deviceIndex, int maskVal, bool createFlag)
{
	struct
	{
		struct nlmsghdr nl;
	        struct ifaddrmsg rt;
     		char buf[IFLIST_REPLY_BUFFER];
	} newIPreq;
	//int fd;
	struct sockaddr_nl la;
	struct sockaddr_nl pa;
	struct msghdr msg;
	struct iovec iov;
	struct rtattr *rtap;
		
	int rbuf,sbuf;
	int ifal;
	socklen_t sizeLen;
	sizeLen = sizeof(rbuf);
	rbuf = 2256000;
	sbuf = 1156000;
	// open socket
	if(netLinkFd_<0)
	{
		if((netLinkFd_=openFD())<0)
		{
			//print error
			return;
		}
		bzero(&la, sizeof(la));
		la.nl_family = AF_NETLINK;
		la.nl_pid = getpid();

		setsockopt (netLinkFd_, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeLen);
		setsockopt (netLinkFd_, SOL_SOCKET, SO_SNDBUF, &rbuf, sizeLen);
		//deviceIndex_= getDeviceIndex(deviceName);
	    	if(bind(netLinkFd_, (struct sockaddr*) &la, sizeof(la))<0)
		{
			//print error
			return;
		}
	}
	// initalize RTNETLINK request buffer
	bzero(&newIPreq, sizeof(newIPreq));
	// compute the initial length of the
	// service request
	ifal = sizeof(struct ifaddrmsg);
	// add first attrib:
	// set IP addr
	// RTNETLINK buffer size
	rtap = (struct rtattr *) newIPreq.buf;
	rtap->rta_type = IFA_ADDRESS;
	rtap->rta_len = sizeof(struct rtattr) + 4;
	inet_pton(AF_INET, startingIP, ((char *) rtap) + sizeof(struct rtattr));
	ifal += rtap->rta_len;

	// add second attrib:
	// set ifc index and increment the size
	rtap = (struct rtattr *) (((char *) rtap) + rtap->rta_len);
	rtap->rta_type = IFA_LOCAL;
	rtap->rta_len = sizeof(struct rtattr) + 4;
	inet_pton(AF_INET, startingIP, ((char *) rtap) + sizeof(struct rtattr));
	ifal += rtap->rta_len;
	// setup the NETLINK header
	newIPreq.nl.nlmsg_len = NLMSG_LENGTH(ifal);
	/* TODO: test with NLM_F_APPEND */
	if(createFlag)
	{
		newIPreq.nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_APPEND;
		newIPreq.nl.nlmsg_type = RTM_NEWADDR;
	}
	else
	{
		newIPreq.nl.nlmsg_flags = NLM_F_REQUEST;
		newIPreq.nl.nlmsg_type = RTM_DELADDR;
	}

	// setup the service header (struct rtmsg)
	newIPreq.rt.ifa_family = AF_INET;
	newIPreq.rt.ifa_prefixlen = maskVal; 
	newIPreq.rt.ifa_flags = IFA_F_SECONDARY;
	newIPreq.rt.ifa_scope = RT_SCOPE_HOST;
	/*TODO: think how can you find out the ifa_index */
	newIPreq.rt.ifa_index = deviceIndex;
	
	// create the remote address
	// to communicate
	bzero(&pa, sizeof(pa));
	pa.nl_family = AF_NETLINK;

	// initialize & create the struct msghdr supplied
	// to the sendmsg() function
	bzero(&msg, sizeof(msg));
	msg.msg_name = (void *) &pa;
	msg.msg_namelen = sizeof(pa);

	// place the pointer & size of the RTNETLINK
	// message in the struct msghdr
	iov.iov_base = (void *) &newIPreq.nl;
	iov.iov_len = newIPreq.nl.nlmsg_len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	// send the RTNETLINK message to kernel
	
	int ret = sendmsg(netLinkFd_, &msg, MSG_DONTWAIT);
	//cout << "ret :: " <<ret<< endl;
	// close socket
	//close(fd);
}

void AdminListener::addOrDeleteIpv6ToHost(string& startingIP, string& device, int maskVal,bool createFlag)
{

	int ret = 1;
    IFLOG(logManager_,LoggerManager::Level3)
    {
       string tmp = "Received request to delete ipv6...";
       logManager_->logString(tmp,LoggerManager::Level3);
    }
   // if (!createFlag)
    {
        try
        {
	        string routeCommand = "";
            routeCommand = "ip -6 addr del "+startingIP+"/"+CommonUtilFuncs::itoa(maskVal)+" dev "+device ;
            ret = system(routeCommand.c_str());
		}
        catch(...)
	    {
            
            IFLOG(logManager_,LoggerManager::Level3)
            {
			   string tmp = "Failed to delete  IPV6 addresses";
               logManager_->logString(tmp,LoggerManager::Level1);
            }
        }
   }

}

int AdminListener::getDeviceIndex(string &deviceName)
{
    struct ifreq ifr;
	int if_name_len=strlen(deviceName.c_str());
    if (if_name_len<sizeof(ifr.ifr_name))
    {
        memcpy(ifr.ifr_name,deviceName.c_str(),if_name_len);
        ifr.ifr_name[if_name_len]=0;
    }
    else
    {
        string tmp = "Interface name is too long";
		TRACELN(Trace::Debug, tmp);
    }
    int fd=socket(AF_UNIX,SOCK_DGRAM,0);
    if (fd==-1)
    {
	    string tmp = "Error in FD open";
		TRACELN(Trace::Debug, tmp);
    }
    if (ioctl(fd,SIOCGIFINDEX,&ifr)==-1)
    {
		string tmp = "IOCTL Command failed";
		TRACELN(Trace::Debug, tmp);
    }
	close (fd);
    return ifr.ifr_ifindex;
}
char* AdminListener::increment_address(const char* address_string, int step)
{
    // convert the input IP address to an integer
    in_addr_t address = inet_addr(address_string);

    // add one to the value (making sure to get the correct byte orders)
    address = ntohl(address);
    address += step;
    address = htonl(address);

    // pack the address into the struct inet_ntoa expects
    struct in_addr address_struct;
    address_struct.s_addr = address;

    // convert back to a string
    return inet_ntoa(address_struct);
}
void AdminListener::addOrDeleteRouteIPV4(string &startingIP, int maskVal, string& device, bool isAdd)
{
	string netmask;
	string gateway = "1.1.1.254";
	string routeCommand = "";
	unsigned int temp = 0xffffffff;
	int bit = 32 - maskVal;
	temp = temp << bit;
	string sTemp = Integer::toString(temp,16);
	netmask = CommonUtilFuncs::hextodottedIPAddr((char*)sTemp.c_str());
	string network = CommonUtilFuncs::getNetIpAddress((char*)startingIP.c_str(),(char*)netmask.c_str());
	if(isAdd)
	{
		if(stricmp(sysInfo_.boardType_.c_str(),"TravelHawk") == 0)
			routeCommand = "route add -net "+network+" netmask "+netmask+" gw "+gateway+" dev "+device ; 
		else
			routeCommand = "route add -net "+network+" netmask "+netmask+" dev "+device ;
	}
	else
	{
		if(stricmp(sysInfo_.boardType_.c_str(),"TravelHawk") == 0)
			routeCommand = "route del -net "+network+" netmask "+netmask+" gw "+gateway+" dev "+device ; 
		else
			routeCommand = "route del -net "+network+" netmask "+netmask+" dev "+device ;
	}
    system(routeCommand.c_str());
}
void AdminListener::generateIPV4Address(AdminListenerConnectionPort* admin,string &device, string &netMask, string &startingIP, int noOfIP, int step)
{
	int ret = 1;
	int bktSz = 0;
	TRACELN(Trace::Debug, "Received request to generate_ip...");
	IFLOG(logManager_,LoggerManager::Level3)
	{
		string tmp = "Received request to generate_ip...";
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	if (currentAdmin_)
	{
#ifndef WIN32
		try
		{

			int maskVal = atoi(netMask.c_str());
			int deviceIndex = getDeviceIndex(device);
			for (int i=1; i<=noOfIP; i++)
			{ 
				if(i == 1)
					addIP2PrimaryBucket(startingIP,4);

				addOrDeleteIpv4ToHost(startingIP.c_str(), deviceIndex, maskVal, true);
				startingIP = increment_address (startingIP.c_str(),step);
			}
			addOrDeleteRouteIPV4(startingIP,maskVal,device,true);
			string tmp ;
			tmp = "Successfully generated ip(s).";
			if(runnerType_ != ADMIN_RUNNER::EastflexRunner)
				sendResponse(admin,ADMIN_CMD::GENERATE_IP_RESPONSE,status_,tmp);
			TRACELN(Trace::Debug, "IP generated...");
			IFLOG(logManager_,LoggerManager::Level3)
			{
				string tmp = "IP generated......";
				logManager_->logString(tmp,LoggerManager::Level3);
			}
		}
		catch(...)
		{
			string tmp = "Failed to generate IP addresses at " + getLoadServerName();
			sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
			TRACELN(Trace::Debug, tmp);
			IFLOG(logManager_,LoggerManager::Level3)
			{
				logManager_->logString(tmp,LoggerManager::Level1);
			}
		}
#endif
	}
}

void AdminListener::generateIPV6Address(AdminListenerConnectionPort* admin,string &device, int prefixVal, string
				&startingIP, int noOfIP, int noHexCharFlagVal, int ipStep)
{
	int ret = 1;
	int bktSz = 0;
	TRACELN(Trace::Debug, "Received request to generate_ip...");
	IFLOG(logManager_,LoggerManager::Level3)
	{
		string tmp = "Received request to generate_ip...";
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	if (currentAdmin_)
	{
#ifndef WIN32
		try
		{
			IPAddress_t tmpIpv6(startingIP);
			int deviceIndex = getDeviceIndex(device);
			pid_t pid = getpid();
			if(netLinkFd_>=0)
			{
				close(netLinkFd_);
				netLinkFd_=-1;
			}
			for (int count=0; count< noOfIP; count++)
			{
				if(bktSz == 256)
					bktSz = 0;
				NetLinkUtils::ipv6AddressAddition2Dev(deviceIndex, tmpIpv6, prefixVal, pid);
				if(bktSz == 0)
					add2IPBucket(tmpIpv6.v6str(),6);
				tmpIpv6.increment(ipStep);
				bktSz++;
			}
			NetLinkUtils::closeFD();
			string tmp ;
			tmp = "Successfully generated ip.";
			sendResponse(admin,ADMIN_CMD::GENERATE_IP_RESPONSE,status_,tmp);
			TRACELN(Trace::Debug, "IP generated...");
			IFLOG(logManager_,LoggerManager::Level3)
			{
				string tmp = "IP generated......";
				logManager_->logString(tmp,LoggerManager::Level3);
			}
		}
		catch(...)
		{
			string tmp = "Failed to generate IP addresses at " + getLoadServerName();
			sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
			TRACELN(Trace::Debug, tmp);
			IFLOG(logManager_,LoggerManager::Level3)
			{
				logManager_->logString(tmp,LoggerManager::Level1);
			}
		}
#endif
	}
}
void AdminListener::clearIPAddress(string& device)
{
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) 
    {
        perror("getifaddrs");
        return;
    }
	int deviceIndx = getDeviceIndex(device);

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_addr == NULL)
            continue;  

        s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if((strcmp(ifa->ifa_name,device.c_str())==0)&&(ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                return;
            }
           // printf("\tInterface : <%s>\n",ifa->ifa_name );
            //printf("\t  Address : <%s>\n", host); 
			//cout<<"ifa->ifa_name :: "<<ifa->ifa_name<<endl;
			//cout<<"host :: "<<host<<endl;
			if(!(stricmp(host,"1.1.1.1") == 0))
				addOrDeleteIpv4ToHost(host,deviceIndx,16,false);
        }
    }

    freeifaddrs(ifaddr);
}
void AdminListener::clearIPAddress(AdminListenerConnectionPort* admin,string &device)
{
	int ret = 1;
	TRACELN(Trace::Debug, "Received request to clear_ip...");
	IFLOG(logManager_,LoggerManager::Level3)
	{
		string tmp = "Received request to clear_ip...";
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	if (currentAdmin_)
	{
#ifndef WIN32
		try
		{
			string command = "/sbin/ifdown " + device;
			system(command.c_str());
			command = "/sbin/ifup " + device;
			system(command.c_str());
			string tmp ;
			tmp = "Successfully cleared virtual ip addresses.";
			if(runnerType_ != ADMIN_RUNNER::EastflexRunner)
				sendResponse(admin,ADMIN_CMD::CLEAR_IP_RESPONSE,status_,tmp);
			TRACELN(Trace::Debug, "Virtual IP cleared...");
			IFLOG(logManager_,LoggerManager::Level3)
			{
				string tmp = "Virtual IP cleared......";
				logManager_->logString(tmp,LoggerManager::Level3);
			}
		}
		catch(...)
		{
			string tmp = "Failed to clear virtual IP addresses at " + getLoadServerName();
			sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
			TRACELN(Trace::Debug, tmp);
			IFLOG(logManager_,LoggerManager::Level3)
			{
				logManager_->logString(tmp,LoggerManager::Level1);
			}
		}
#endif
	}
}

void AdminListener::routeIPAddress(AdminListenerConnectionPort* admin,string &device,string &netmask,string &gateway,string &netIP)
{
	int ret = 1;
    TRACELN(Trace::Debug, "Received request to route_ip...");
    IFLOG(logManager_,LoggerManager::Level3)
    {
       string tmp = "Received request to route_ip...";
       logManager_->logString(tmp,LoggerManager::Level3);
    }
    if (currentAdmin_)
    {
#ifndef WIN32
        try
        {
        	string routeCommand = "";
        	int netbit = atoi(netmask.c_str());
        	string mask = CommonUtilFuncs::getNetmaskFromNetbit(netbit);
        	string networkAddress = CommonUtilFuncs::getNetIpAddress((char*)netIP.c_str(),(char*)mask.c_str());

            routeCommand = "route add -net "+networkAddress+" netmask "+mask+" gw "+gateway+" dev "+device ;
            system(routeCommand.c_str());
            string tmp ;
            //tmp = "Successfully routed ip addresses.";
            tmp = "Successfully added IP route for :: "+networkAddress;
            if(routeCommand != "")
            sendResponse(admin,ADMIN_CMD::ROUTE_IP_RESPONSE,status_,tmp);
            TRACELN(Trace::Debug, "Virtual IP routed...");
            IFLOG(logManager_,LoggerManager::Level3)
            {
               string tmp = "Virtual IP routed......";
               logManager_->logString(tmp,LoggerManager::Level3);
            }
		}      
        catch(...)
	    {		
            string tmp = "Failed to route IP addresses at " + getLoadServerName();
            sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
            TRACELN(Trace::Debug, tmp);
            IFLOG(logManager_,LoggerManager::Level3)
            {
               logManager_->logString(tmp,LoggerManager::Level1);
            }
        }                  
    #endif
   }
}

void AdminListener::routeIPV6Address(AdminListenerConnectionPort* admin,string &netIP,string &gateway,string &device,string &prefix)
{
    int ret = 1;
    TRACELN(Trace::Debug, "Received request to route_ip...");
    IFLOG(logManager_,LoggerManager::Level3)
    {
       string tmp = "Received request to route_ip...";
       logManager_->logString(tmp,LoggerManager::Level3);
    }
    if (currentAdmin_)
    {
#ifndef WIN32
        try
        {
	        string routeCommand = "";
            routeCommand = "route -A inet6 add "+netIP+"/"+prefix+" gw "+gateway+" dev "+device ;  
            system(routeCommand.c_str());
            string tmp ;
            //tmp = "Successfully routed ip addresses.";
            tmp = "Successfully added IP route for :: "+netIP;
			if(routeCommand != "")
            sendResponse(admin,ADMIN_CMD::ROUTE_IPV6_RESPONSE,status_,tmp);
            TRACELN(Trace::Debug, "Virtual IP routed...");
            IFLOG(logManager_,LoggerManager::Level3)
            {
               string tmp = "Virtual IP routed......";
               logManager_->logString(tmp,LoggerManager::Level3);
            }
		}      
        catch(...)
	    {		
            string tmp = "Failed to route IP addresses at " + getLoadServerName();
            sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
            TRACELN(Trace::Debug, tmp);
            IFLOG(logManager_,LoggerManager::Level3)
            {
               logManager_->logString(tmp,LoggerManager::Level1);
            }
        }                  
    #endif
   }
  
}

void AdminListener::delRouteIPV4(AdminListenerConnectionPort* admin,string &device,string &netIP,string &netmask,string &gateway)
{
	int ret = 1;
    TRACELN(Trace::Debug, "Received request to delete route_ip...");
    IFLOG(logManager_,LoggerManager::Level3)
    {
       string tmp = "Received request to delete route_ip...";
       logManager_->logString(tmp,LoggerManager::Level3);
    }
    if (currentAdmin_)
    {
#ifndef WIN32
        try
        {
	        string routeCommand = "";
            string ipAddr = CommonUtilFuncs::dottedIPAddrtoHex((char*)netIP.c_str());
            string mask = CommonUtilFuncs::dottedIPAddrtoHex((char*)netmask.c_str());
            unsigned int ipAddress = Integer::parseInt(ipAddr.erase(0,1),16);
            unsigned int maskVal = (unsigned int)Integer::parseInt(mask.erase(0,1),16);
            unsigned int networkVal = ipAddress & maskVal;
            string networkAddr = Integer::toString(networkVal,16);
            string networkAddress = CommonUtilFuncs::hextodottedIPAddr((char*)networkAddr.c_str());

            routeCommand = "route del -net "+networkAddress+" netmask "+netmask+" gw "+gateway+" dev "+device ;  
            system(routeCommand.c_str());
            string tmp ;
            //tmp = "Successfully deleted ip addresses.";
           // sendResponse(admin,ADMIN_CMD::ROUTE_IP_RESPONSE,status_,tmp);
            TRACELN(Trace::Debug, "Virtual IP deleted...");
            IFLOG(logManager_,LoggerManager::Level3)
            {
               string tmp = "Virtual IP deleted......";
               logManager_->logString(tmp,LoggerManager::Level3);
            }
		}      
        catch(...)
	    {		
            string tmp = "Failed to delete IP addresses at " + getLoadServerName();
            sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
            TRACELN(Trace::Debug, tmp);
            IFLOG(logManager_,LoggerManager::Level3)
            {
               logManager_->logString(tmp,LoggerManager::Level1);
            }
        }                  
    #endif
   }
}
void AdminListener::clearRouteIPV6(AdminListenerConnectionPort* admin,string &netIP,string &gateway,string &device,string &prefix)
{
	int ret = 1;
    TRACELN(Trace::Debug, "Received request to delete route_ipv6...");
    IFLOG(logManager_,LoggerManager::Level3)
    {
       string tmp = "Received request to delete route_ipv6...";
       logManager_->logString(tmp,LoggerManager::Level3);
    }
    if (currentAdmin_)
    {
#ifndef WIN32
        try
        {
	        string routeCommand = "";
            routeCommand = "route -A inet6 del "+netIP+"/"+prefix+" gw "+gateway+" dev "+device ;  
            system(routeCommand.c_str());
            string tmp ;
            //tmp = "Successfully deleted routed ipv6 addresses.";
            //sendResponse(admin,ADMIN_CMD::ROUTE_IPV6_RESPONSE,status_,tmp);
            TRACELN(Trace::Debug, "Virtual IPV6 routing deleted...");
            IFLOG(logManager_,LoggerManager::Level3)
            {
               string tmp = "Virtual IPV6 routing deleted......";
               logManager_->logString(tmp,LoggerManager::Level3);
            }
		}      
        catch(...)
	    {		
            string tmp = "Failed to delete route IPV6 addresses at " + getLoadServerName();
            sendResponse(admin,ADMIN_CMD::FAILURE,status_,tmp);
            TRACELN(Trace::Debug, tmp);
            IFLOG(logManager_,LoggerManager::Level3)
            {
               logManager_->logString(tmp,LoggerManager::Level1);
            }
        }                  
    #endif
   }
  

}
  

void AdminListener::getUniqueId(string &uniqueID)
{
	double curTimeinMilli = CommonUtilFuncs::getSysTime();
	char buffer[64];
	sprintf(buffer,"%13.0f",curTimeinMilli*1000);
	uniqueID = buffer;
}
void AdminListener::getNetworkInterfaces(string &networkInterfaces)
{
		networkInterfaces = "";
		string uniqueID = "";
		
		getUniqueId(uniqueID);
		string opFile ="netdev_" + uniqueID + ".txt";
		string cmd = "/bin/grep : /proc/net/dev | cut -f 1 -d\":\" | grep eth > " + opFile;
		system(cmd.c_str());
		
		ifstream input(opFile.c_str());
		string devName = "";
		input >> devName;
		while(input)
		{
			if (networkInterfaces != "")
				networkInterfaces += ",";
			networkInterfaces += devName;
			input >> devName;
		}
		input.close();
		unlink(opFile.c_str());
}


int AdminListener::getEventServerHostPort( AdminListenerConnectionPort* admin, string & host, int &port, string & error_string )
{
	int result;
#ifdef WIN32
	result = SetCurrentDirectory(admin->eastHome_.c_str());
#else
	result= chdir((char*)admin->eastHome_.c_str());
	if(result != -1)
		result = 1;
	else
        result =0;
#endif
	if(!result ) 
	{
		error_string += "Unable to set the current dir :: "+ admin->eastHome_ ;
		error_string += "::please check the remotehost.config.";
		return result ;
	}
	IFLOG(logManager_,LoggerManager::Level3)
	{
		string tmp = "East home  = "+admin->eastHome_;
		logManager_->logString(tmp,LoggerManager::Level3);
	}
	string ini = "2.1.1.ini";
	result = environment_.open(ini);
	//after seting the new directory there need to read the desktop dos.
	if(!result)
	{
		error_string +="2.1.1.ini file not found." ;
		return result ;
	}
	Environment::readProperties("desktop.DOS");
	host = Environment::getProperty("EventServerHost");
	string serverport =Environment::getProperty("EventServerPort");
	port  = atoi((const char*)serverport.c_str());
}

void AdminListener::readBladeInfo()
{
	string::size_type pos1,pos2, pos3;
	pos1=pos2=pos3=0;
	// In actual the SYSTEM_INFO.txt will be available in "/root/.#/.hdinfo"
	std::string file = "";
	std::string vFile = "";
	fstream ft;
	file = "/root/.#/.SYSTEM_INFO/BLADE_INFO.txt"; //Get the actual location of the file from the local production team
	vFile = "/root/.#/.SYSTEM_INFO/BLADE_INFO_VM.txt"; //Hotwire and other simulation on vmware
	ft.open((char*)vFile.c_str());
	if(ft.is_open())
	   file=vFile; 
    

	ConfigFileReader *systemInfoReader = new ConfigFileReader(file); 
	systemInfoReader->readProperties(file);
	
	sysInfo_.hasRTM_ = 0;

	sysInfo_.localHostName_			= systemInfoReader->getProperty("host_name");			
	sysInfo_.boardType_				= systemInfoReader->getProperty("board_type");			
	sysInfo_.macID_					= systemInfoReader->getProperty("mac_id");				
	sysInfo_.bootEth_				= systemInfoReader->getProperty("boot_eth");			
	sysInfo_.osImage_				= systemInfoReader->getProperty("image");				
	sysInfo_.shelfMgr_				= systemInfoReader->getProperty("shelf_manager");		
	//sysInfo_.slotNo_				= systemInfoReader->getProperty("slot_no");				
	sysInfo_.bootType_				= systemInfoReader->getProperty("boot_type");			

	sysInfo_.removableDev1_			= systemInfoReader->getProperty("removable_dev1");		
	pos1 = sysInfo_.removableDev1_.find("RTM",0);
	sysInfo_.removableDev1_Status_	= systemInfoReader->getProperty("removable_dev1_cs");		
	
	sysInfo_.removableDev2_			= systemInfoReader->getProperty("removable_dev2");		
	pos2 = sysInfo_.removableDev2_.find("RTM",0);
	sysInfo_.removableDev2_Status_	= systemInfoReader->getProperty("removable_dev2_cs");	
	
	sysInfo_.removableDev3_			= systemInfoReader->getProperty("removable_dev3");		
	pos3 = sysInfo_.removableDev3_.find("RTM",0);
	sysInfo_.removableDev3_Status_	= systemInfoReader->getProperty("removable_dev3_cs");	

	//sysInfo_.storage_				= systemInfoReader->getProperty("");
	sysInfo_.npuIP_					= systemInfoReader->getProperty("npu_ip");
	
	sysInfo_.eventServerIP_			= systemInfoReader->getProperty("east_eventip");
	if((pos1 != string::npos) || (pos2 != string::npos) || (pos3 != string::npos))
		sysInfo_.hasRTM_ = 1;

	sysInfo_.isPowerHouseChassis_ = systemInfoReader->getProperty("powerhouse_chassis");
	sysInfo_.powerHouseChassisID_ = systemInfoReader->getProperty("powerhousechassis_id");
	//if(strcmp(sysInfo_.npuIP_.c_str(),"NA") == 0)
		//sysInfo_.npuIP_ = "0.0.0.0";

	//determin the number of processors available based upon the boardType_
	if(strcmp(sysInfo_.boardType_.c_str(),"AT8020") == 0)
		sysInfo_.noOfProcessors_ = 4;
	else if(stricmp(sysInfo_.boardType_.c_str(),"AT8050") == 0)
		sysInfo_.noOfProcessors_ = 8;
	else if(stricmp(sysInfo_.boardType_.c_str(),"AT8060") == 0)
		sysInfo_.noOfProcessors_ = 32;
	else 
		sysInfo_.noOfProcessors_ = 1;	
}

void AdminListener::removeConnection(NetworkPort* port)
{
	vector <NetworkPort*>::iterator itr;
	itr = find(networkports_.begin(), networkports_.end(), port);
	if (itr != networkports_.end())
	{
		networkports_.erase(itr);
		if(port  == client_)
			client_ = 0;
		if(runnerType_ == ADMIN_RUNNER::EastflexRunner)
		{
			if(port == currentAdmin_)
			{
				current_state_ = ADMIN_STATE::AVAILABLE;
				status_ = ADMIN_STATE::AVAILABLE;
				runnerType_ = -1;
				currentAdmin_ = 0;
				ownerId_  = "NONE";
				ownerLoginUser_ = "";
				loadServerName_ = "";
				desktopStarted_  = false;
				isDesktopRunning_ = false;
			}
		}
	}
}

void AdminListener::dumpStartnEndInfoToLogFile(int state)
{
	//Open the Log File in Append Mode so all START and STOP transaction will be recorded.
	//The file should be created under the /root directory - File Name as .EvenetServer_Logs.txt
	//Dump during Start: Event Server PID :: The Start Time of this instance EventServer 
	//Dump during END::  Event Server PID :: The End Time   of this instance of EventServer
	//As Most of the time AmdinListener is killed through KILL command STOP 
	FILE* fp;
	time_t tTM;
	string combinedStr;
	struct tm* currTM;
	
	//Get PID
	int adminPID = getpid(); 
	string sPID = CommonUtilFuncs::itoa(adminPID);
	
	//Get Time and Format it 
	time(&tTM);
	currTM = localtime(&tTM);
	char timeStamp[100];
	if(state == START)
		strftime (timeStamp, 100, "----START Time::%d-%m-%Y %H:%M:%S\n", currTM);
	if(state == STOP)
		strftime (timeStamp, 100, "----STOP  Time::%d-%m-%Y %H:%M:%S\n", currTM);
		
	combinedStr ="Admin PID::"+sPID+string(timeStamp);
	string fileName = "/root/.EvenetServer_Logs.txt";
	fp=fopen((char *)fileName.c_str(), "a");
	if(!fp)
	{
//		cout<<fp<<endl;
		return;
	}
	int strLen = combinedStr.length();
	fputs((char*)combinedStr.c_str(),fp);

	fclose(fp);
}

int AdminListener::handleRESTCommand(AdminListenerConnectionPort* admin)
{
	string command = "";
	currentAdmin_ = admin;
	istr_.get(command);
	if(command == "startdesktop")
	{	
		string hostname = "";
		string easthome = "";
		string javahome = "";

		istr_.get(hostname);
		istr_.get(restPort);
		istr_.get(easthome);
		istr_.get(javahome);

		admin->adminId_ = "localhost";
		admin->osname_ = "Linux"; // also can check at RESTAPIManager which OS it's running
		admin->serverName_ = hostname;
		loadServerName_ = admin->serverName_;
		admin->eastHome_ = easthome;
		admin->javaHome_ = javahome;


		invokedesktop_ = true;

		startDesktop(admin);
	}
	else if(command == "stopdesktop")
	{
		string hostname = "";

                istr_.get(hostname);
                admin->adminId_ = "localhost";
                admin->osname_ = "Linux"; // also can check at RESTAPIManager which OS it's running
                admin->serverName_ = hostname;
                loadServerName_ = admin->serverName_;

		mySleep(10);
		stopRunner();
		mySleep(10);//eastflex execution taking time to stop......
		sendStopDesktopRequest();
		isDesktopRunning_ = false;
	}
	/*else if(command == "startrunner")
	{
		string restport = "";
		istr_.get(restport);

		cout<<"restport : "<<restport<<endl;
		startRunner(restport);
	}*/
}
void AdminListener::initRMInterface()
{
		TRACELN(Trace::Debug, "NPGInterface Initiating Corba Connection.");
		rmClient_.initialize();
		std::string error = rmClient_.getDetailErrorMessage();
		//rmClient_.setKeepAliveFrequencyTime(30);
		if(error == "")
		{
			TRACELN(Trace::Debug, "Corba Connection Status :: SUCCESS.");
			TRACELN(Trace::Debug,"RM Client intialization SUCCESS");
		}
		else
		{	
			TRACELN(Trace::Debug, "Corba Connection Status :: FAILURE.");
			error = rmClient_.getDetailErrorMessage();
			string tmp  = "Detail Error Message is "+error;
			TRACELN(Trace::Debug,tmp );
		}
}
void AdminListener::add2IPBucket(string startingIP,int ipType)
{
	if(ipType == 4)
		ipBucketV4_.push_back(startingIP);
	if(ipType == 6)
		ipBucketV6_.push_back(startingIP);
}
void AdminListener::addIP2PrimaryBucket(string startingIP,int ipType)
{
	if(ipType == 4)
	{
		ipPrimaryBucketV4_.push_back(startingIP);
		startingIP = increment_address (startingIP.c_str(),1);
		ipSecondaryBucketV4_.push_back(startingIP);
	}
}
void AdminListener::clearIPBuckets()
{
	ipPrimaryBucketV4_.clear();
	ipSecondaryBucketV4_.clear();
	ipBucketV4_.clear();
	ipBucketV6_.clear();
}
void AdminListener::clearIPBucketSecondary(AdminListenerConnectionPort* admin, string devName,int maskVal, int ipType)
{
	vector<string> *ipBucket;
	vector<string>::reverse_iterator it;
    string msg="";
	int loopCnt = 0;
    int maxBucktSize = 4000;
    //We can even Count against each testbed how many IPs are kept , but that would need an array. So now I have kept the max SCTP association
    //That we allow per testbed
    string ipAddr = "";
    string seriesType = "secondary";
    IPAddress_t ipv6Addr;
    int cleaningPrimary = 0;
    int devIdx = getDeviceIndex(devName);
    if(ipType == 4)
    	ipBucket  = &ipSecondaryBucketV4_;
    if(ipType == 6)
    	ipBucket = &ipBucketV6_;

    int sz = ipBucket->size();
    int pid = getpid();
	string cmdName = ""; //Open this if you need the command of IP clear on per bucket bassis to be written to a script file.
	if(sz > 0)
	{
		//Clear the Secondary Bucket
		for(it = ipBucket->rbegin(); ; )
		{
			if(cleaningPrimary == 1)
			{
				ipBucket = &ipPrimaryBucketV4_;
				it = ipBucket->rbegin();
				maxBucktSize = 1;
				cleaningPrimary = 2;
				seriesType = "primary";
			}
			ipAddr = (string) *it;
			msg="Cleaning IP Address for series ("+seriesType+") ::"+ipAddr;
			sendEastFlexResponse(admin,ADMIN_CMD::EASTFLEX_STATUS_EVENT,msg);
			if(stricmp(seriesType.c_str(),"secondary") == 0)
			{
				system("ip addr flush secondary");
			}
			else
			{  //Sourabh::2311
				int iDevNum=1;
				string bType = sysInfo_.boardType_;

				if(stricmp(bType.c_str(),"vPE") == 0 )
					devName="testport";
				else
				{
					devName="eth";
					iDevNum=6;
				}
				for(int i = iDevNum; i < 7 ; i++)
				{
					cmdName = "ip addr flush ";
					cmdName += devName;
					cmdName += CommonUtilFuncs::itoa(i);
					cmdName += " 2> /dev/null";
					cout<<"cmdName :: "<<cmdName<<endl;
					system(cmdName.c_str());
				}
			}
			it++;
			if(it != ipBucket->rend())
				continue;
			else if(it == ipBucket->rend() && cleaningPrimary == 0) //This says the SecondaryBucket is clean now, Assign the Primary Bucket here
				cleaningPrimary = 1;//Primry Bucket Deletion Started
			else
				return;
		}
	}
}


void AdminListener::clearIPBucket(string devName,int maskVal, int ipType)
{
	vector<string> *ipBucket;
	vector<string>::reverse_iterator it;
    int loopCnt = 0;
    string ipAddr = "";
    IPAddress_t ipv6Addr;

    int devIdx = getDeviceIndex(devName);
    if(ipType == 4)
    	ipBucket = &ipBucketV4_;
    if(ipType == 6)
    	ipBucket = &ipBucketV6_;
    int sz = ipBucket->size();
    int pid = getpid();
	//string cmdName = ""; //Open this if you need the command of IP clear on per bucket bassis to be written to a script file.
	if(sz > 0)
	{
		for(it = ipBucket->rbegin(); it != ipBucket->rend(); it++)
		{
			ipAddr = (string) *it;
			//cmdName = "./a.out "+devName+" "+ipAddr+" 2 255";
			//cout<<cmdName<<endl;
			for(loopCnt = 0 ; loopCnt <= 256 ; loopCnt++)
			{
				if(ipType == 4)
				{
					addOrDeleteIpv4ToHost(ipAddr.c_str(), devIdx, maskVal,false);
					ipAddr  = increment_address (ipAddr.c_str(),1);
				}
				else // ipType ==6)
				{
					ipv6Addr = ipAddr;
					NetLinkUtils::ipv6AddressAddition2Dev(devIdx, ipv6Addr, maskVal, pid,0);
					ipv6Addr.increment(1);
					ipAddr = ipv6Addr.v6str();
				}
			}
			usleep(2000);
		}
	}
}


void parseArgumentsAndInitialize(int argc, char** args)
{
	string hostname="";
	int port = 1250;
	int loglevel = 0;
	int eventServerPort = 1234 ;
	string eventServerHost="";
	bool startEventServer = false;
	bool invokedesktop=true;
	bool clientMode = false;
	bool checkLocalMachine = true;
	string logfile="";
	for (int index = 0; index < argc; index++) 
	{
		std::string arg;
		arg = args[index];
		if ( arg == "-help" ||arg == "?" ) 
		{
			cout << "Usage: \nAdminListener -h [host] -p [port] -l [loglevel] -v [version] -ce [connecteventserver] -nd [nodesktop] -c [client]  "<< endl ;
			exit(0);
		}
		if ( arg == "-v" ||arg == "-version" ) 
		{
			cout << System::getVersion()<< endl ;
			exit(0);
		}
		if ( arg == "-h" ||arg == "-host" ) 
		{
			index++;
			hostname = args[index];
		}
		else if ( arg == "-p" || arg == "-port")
		{
			index++;
			string strprt = args[index];
			port=atoi(strprt.c_str());
		}
		if ( arg == "-eh" ||arg == "-eventserverhost" ) 
		{
			index++;
			eventServerHost= args[index];
		}
		else if ( arg == "-ep" || arg == "-eventserverport")
		{
			index++;
			string strprt = args[index];
			eventServerPort=atoi(strprt.c_str());
		}
		else if ( arg == "-l" || arg == "-loglevel")
		{
			index++;
			string strprt = args[index];
			loglevel=atoi(strprt.c_str());
		}
		else if ( arg == "-ce" || arg == "-connecteventserver")
		{
			startEventServer =  true;
		}
		else if ( arg == "-nd" || arg == "-nodesktop")
		{
			invokedesktop =  false;
		}
		else if ( arg == "-c" || arg == "-client")
		{
			clientMode =  true;
		}
		else if(arg == "-logfile")
		{
			index++;
			logfile = args[index];
		}
		else if ( arg == "-clm" || arg == "-checklocalmachine")
		{
			index++; 
			string flag = args[index];
			if(flag == "false")
			checkLocalMachine = false;
		}

    }
	if(hostname=="")
	{
		const int size = 64;
		char name[size];
		int result = gethostname(name,size);
		hostname = name ;
	}
	if(eventServerHost=="")
	{
		eventServerHost = hostname;
	}

	adminListener = new AdminListener(hostname, port);
	adminListener->readBladeInfo();
	adminListener->setLogLevel(loglevel);
	adminListener->setLogFile(logfile);
	adminListener->start();
	adminListener->invokedesktop_ = invokedesktop;
	adminListener->clientMode_ = clientMode;
	adminListener->checkLocalMachine_ = checkLocalMachine;
	
	adminListener->dumpStartnEndInfoToLogFile(START);
	
	if(adminListener->getFD() != -1)
	{
		if(startEventServer)
			adminListener->connectEventServer();
		else
			adminListener->setEventServerHostPort(eventServerHost,eventServerPort);
		adminListener->waitOnSelect(60);
	}
	else
	{
		cout<<"Could not start AdminListener for host :"<<hostname<<" and port "<<port<<endl;
	}
	adminListener->dumpStartnEndInfoToLogFile(STOP);
}
INT32 RMmain(INT32 argc, CHAR* argv[])
{
	parseArgumentsAndInitialize(argc, (char**)argv);
}
int main(int argc, char** argv,char** envs) 
{
	CommonUtilFuncs::init_tcp();
	TRACE_START(argc,argv);
	// Ashok A:  It should read the hostname/ip address from command line, because the machine 
	// might have multiple ip address.
	bool foundOSArgs=false;
	 os_error error=0;
	int n_argc = 0;
char** arr_argv;

    for(int i=0;i < argc;i++)
    {
          string arg=argv[i];
          if(arg == "-os" && ((i+1) < argc))
          {
				foundOSArgs=true;
          }
		  if(!foundOSArgs)
          {
                n_argc=argc+2;
                arr_argv=new char *[n_argc];
                int i=0;
                while(i < argc){
                        arr_argv[i]=argv[i];
                        i++;
                }
                arr_argv[i]="-os";
                fstream chasissFile,slotFile;
                chasissFile.open("/var/run/iwnet.chassis");
                slotFile.open("/var/run/iwnet.slot");
                string osArguments="program=1,cpu=0,slot=";
                char chasiss[5]={},slot[5]={};

                if(!chasissFile.fail() && !slotFile.fail())
                {
                        chasissFile.getline(chasiss,5);
                        slotFile.getline(slot,5);
                        chasissFile.close();
                        slotFile.close();
                        osArguments+=slot;
                        osArguments+=",chassis=";
                        osArguments+=chasiss;
                        arr_argv[i+1]=(char*)osArguments.c_str();
                        if((error = OS_init(RMmain,(INT32)n_argc,(CHAR**) arr_argv)) != 0 )
                        {
                                cerr<<"Could not start OS\n"<<endl;
                                return error;
                        }
                        delete []arr_argv;
                }
                else
                {
                        cerr<<"Couldn't set OS argument"<<endl;
                        cerr<<"Usage -os program=1,cpu=0,slot=<x>,chasiss=<x>"<<endl;
                        return 1;
                }
          }

    }

#ifndef WIN32
	signal(SIGINT,closeDesktop);
#else
 	if(SetConsoleCtrlHandler((PHANDLER_ROUTINE)closeDesktop, TRUE) == FALSE)
 		std::cout<<"Sorry Unable to install the handler!!"<<std::endl;
#endif

	
	return 1;
}



