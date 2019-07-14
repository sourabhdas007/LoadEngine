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
// $Author: pinak.tripathy $
// $Revision: 205314 $
//
/////////////////////////////////////////////////////////////////////



#ifndef AdminListener_hpp
#define AdminListener_hpp
#include "NetworkTCPServerPort.hpp"
#include "Notice.hpp"
#include "INIParser.hpp"
#include <string>
#include <map>
#include "TimerMgr.hpp"
#include "StreamBuf.hpp"
#include "EventConsumer.hpp"
#include "AdminListenerConnectionPort.hpp"
#include "LoggerManager.hpp"
#include "EventProducer.hpp"
#include "ConfigFileReader.hpp"
#include "AdminListenerCommon.hpp"
#include "resid_if.h"
#include "ResManNPGClient.h"

//Sourabh::Dump AdminListener Start and Stop PID and Time stamp
#define START 1
#define STOP 2

using namespace std;
class AdminListener : public NetworkTCPServerPort , NetworkManager 
{
private:
	int status_; //available or not
	int current_state_;	
	//	Chinmaya
	string desktopPath_;

	bool desktopStarted_ ; //Desktop  is it up or not
	string ownerId_;
	string ownerLoginUser_ ;
	IStreamBuf istr_;
	OStreamBuf ostr_;
	AdminListenerConnectionPort* currentAdmin_;
    bool isSocketExits_; 
	int netLinkFd_ ;
	//int deviceIndex_ ;
	struct AutoIpInfo
	{
		string startIp;
		string device;
		int maskVal;
		int step;
		int noOfIPs;
		string ethType;
		string ipType;
		AutoIpInfo()
		{
			startIp = "";
			device ="";
			maskVal =16;
			step =1;
			noOfIPs=1;
			ethType="";
			ipType="IPv4";
		}
	};
	vector<AutoIpInfo>ipGenReq_ ;
	void addOrDeleteRouteIPV4(string & startingIP, int maskVal,string& device, bool isadd);
	vector<string> ipBucketV4_;
	vector<string> ipBucketV6_;
	vector<string> ipPrimaryBucketV4_;
	vector<string> ipSecondaryBucketV4_;

public:
	npg_rm::ResManNPGClient rmClient_ ;
	npg_rm::ResManNPGClient::stdVec_PortInfo portInfoVector_;
	map<string,int> leProcess2ResourceID_;
	string eventServerHost_;
	int eventServerPort_;
	EventConsumer* consumer_;
	EventProducer* producer_ ;

	EventConsumer* localconsumer_;
	EventProducer* localproducer_ ;

	TimerMgr* timerMgr_ ;
	RunningTimer* stopDesktoptimer_;
	RunningTimer* KeepAliveTimer_;
	RunningTimer* desktopStatusTimer_;
	RunningTimer* statusTimer_ ;
    RunningTimer* atrStatusTimer_ ;
	RunningTimer* heartBeatTime_;
	int hbInterval_;
	
	//rest timers
	RunningTimer* restDesktopStatusTimer_;

	unsigned char version_ ;
	bool invokedesktop_; //Seems to be a duplicate of desktopStarted_ 
	bool checkLocalMachine_;
	bool reserveByLocal_;
	vector<int> processIdList_ ;
	LoggerManager* logManager_;
	AdminListener(string host,int port);
	bool isDesktopRunning_ ;
	bool isRunnerRunning_;
	string restPort;
	bool clientMode_;
	string loadServerName_;
	string netInterfaces_;
	string ipRemoteMgntPanel_;
	os_cardnum_t cardNumber_;
	os_cardnum_t cardNumberLE_;
	os_cardnum_t cardNumberSWDG_;
	~AdminListener();
	void read(char* &data, int& length);
	// Ashok s: Overriden the pure virtual methods 
	void received(NetworkPort* port ,char buffer[],int length);
	void received(NetworkPort* port ,char* portIdentifier,char* buffer,int length){}
	void received(char* portIdentifier,char* buffer,int length){}
	void received_sigcompmsg(NetworkPort* port,char* buffer,int length){}
	
	void received(char* portIdentifier,
		char* buffer,int length, std::string key, int value){};
#ifdef WIN32
        void received(NetworkPort* port, char data[],int length, map<string,string> * properties) {};
        void received_sigcompmsg(NetworkPort* port, char* buffer,int length,int &offset,  map<string,string> *properties, bool &pingPong) {};

#else
        void received(NetworkPort* port, char data[],int length, mapHash2Pair *properties) {};
        void received_sigcompmsg(NetworkPort* port, char* buffer,int length,int &offset,  mapHash2Pair *properties, bool &pingPong) {};
#endif

	//added for east_flex
	void handleEastFlexRequest(AdminListenerConnectionPort* admin,int request_type);
	int sendEastFlexResponse(AdminListenerConnectionPort* admin , int command , string result);

	void closed(NetworkPort* port){}
	void addPort(NetworkPort* port){}
	void removePort(NetworkPort* portToRemove){}
	void setEventFd(fd_set& read,int &fds);
	void reserve(AdminListenerConnectionPort* admin );
	void reserve_EastFlex(AdminListenerConnectionPort* admin );	
	void unreserve_EastFlex(AdminListenerConnectionPort* admin );	
	void unreserve(AdminListenerConnectionPort*);
	void startDesktop(AdminListenerConnectionPort* admin); 
	void startDesktopEastFlex(AdminListenerConnectionPort* admin); 	   //SK::08SEPT

	void stopDesktop(AdminListenerConnectionPort* admin);   
	int write(const char* data,const int length , int id = -1){return 0;}
	void write(const char* data, const int length){}
	void readEventFDs(fd_set& read);
	void handleTimeout(RunningTimer* rt);
	void removeConnection(NetworkPort* port);	
	int waitOnSelect(int timetoWait);
	int getTimerBaseName(){return 0;}
	int handleTimeout();
	int sendResponse(AdminListenerConnectionPort* admin , int command , int status, string result);
	int checkAndKillProcess();
	INIParser environment_; 
	int createProcess(AdminListenerConnectionPort* port , string& error_string);
	void readPortFD(fd_set& read, int maxfds) ;
	unsigned int fd_list_[FD_SETSIZE];
	string request2String(int request);
	void setLogLevel(int level);
	void connectEventServer();
	void sendStatus(AdminListenerConnectionPort* admin ) ;
	void sendStopDesktopRequest();
	RunningTimer* reconnectTimer_ ;
	void setEventServerHostPort(string& hostname , int port);
	void initialize();
	void  reconnect();
	int checkEventServerStatus();
	void invokeDesktop(AdminListenerConnectionPort* admin);
	void registerEvents();
	void setLogFile(string logfile);
	void attach(AdminListenerConnectionPort* admin ) ;
	void start();
	static string status2String(int status);
	void processEvent(Notice* notice);
	void handleProcessID(Notice* notice);
	void startLoadEngine(Notice* notice);
	void stopLoadEngine(Notice* notice);
	void bootServer(bool restart);
	string getLoadServerName();
	void generateIPV4Address(AdminListenerConnectionPort* admin,string &device, string &netMask, string &startingIP, 
							int noOfIP,int step = 1);
	void generateIPV6Address(AdminListenerConnectionPort* admin,string &device, int prefixVal, string &startingIP,
							int noOfIP, int noHexCharFlagVal, int step =1);
	void clearIPAddress(AdminListenerConnectionPort* admin,string &device);
    void routeIPAddress(AdminListenerConnectionPort* admin,string &device,string &netmask,string &gateway,string &netIP);
    void routeIPV6Address(AdminListenerConnectionPort* admin,string &netIP,string &gateway,string &device,string &prefix);
	void delRouteIPV4(AdminListenerConnectionPort* admin,string &device,string &netIP,string &netmask,string &gateway);
	void clearRouteIPV6(AdminListenerConnectionPort* admin,string &netIP,string &gateway,string &device,string &prefix);
	void getNetworkInterfaces(string &networkInterfaces);
	void getUniqueId(string &uniqueID);
	bool checkLocalSystem(AdminListenerConnectionPort* admin);
	bool checkLocalSystem();
	int getEventServerHostPort( AdminListenerConnectionPort* admin, string & host, int &port, string & error_string );
	void sendAdminListenerStatusToDesktop();
	void sendAvailableStatusToDesktop();
	
  	void received(NetworkPort* port ,string portIdentifier,char* buffer,int length,string &destHostPort,int &destPort,int type)
{};
	void addOrDeleteIpv4ToHost(const char* startingIP, int deviceIndex, int maskVal,bool createFlag);
	void addOrDeleteIpv6ToHost(string& startingIP, string& deviceIndex, int maskVal,bool createFlag);
	char* increment_address(const char* address_string,int step);
	int getDeviceIndex(string &deviceName);
	void clearIPAddress(string &deviceName);
	int openFD();

	void readBladeInfo();
	struct SystemInfo 
	{
		string localHostName_;			//Host NAme of the balde
		string boardType_;				//ATCA8020 or ATCA8050 other if any
		string macID_;					//Mac ID of the Boot Device
		string bootEth_;				//What is the ethernet Device ID used for booting this board
		string osImage_;				//Image being installed on this balde
		string shelfMgr_;				//Shelf manager for this board
		string slotNo_;					//Slot number where the blade is connected to the Chassis
		string bootType_;				//NFS or Local or SM
		
		string removableDev1_;			//What is connected on Bay1 (i.e. NPU or AMC or Xylo)
		string removableDev1_Status_;	//Current State of the Bay1 removable device 
		string removableDev2_;			//What is connected on Bay2 (i.e. NPU or AMC or Xylo)
		string removableDev2_Status_;	//Current State of the Bay2 removable device 
		string removableDev3_;			//What is connected on Bay3 (i.e. NPU or AMC or Xylo)
		string removableDev3_Status_;	//Current State of the Bay3 removable device 
		string storage_;				//Not Used :: Will Store RTM (in case of Blade with RTM), localdisk (for desktop machines), SM, for blades booted from SM
		string npuIP_;
		int hasRTM_;					//Will store  if the Blade is connected with RTM ::Sourabh
		string eventServerIP_;			//Will store the EventServer IP to be used for Desktop.DOS 
		string powerHouseChassisID_; 	//In case of Power House Chassis it will have the Chassi ID , on a non power House Chassis it will have the Value NA
		//Following variables will be enhanced to ensure that, same EastFlexRunner can load two packages and multiple package shares the same blade OR multiple EastFlexRunner shares the same piece of HW (for example one EastFlexRunenr uses 2 Cores and other uses the remianing 6 cores)
		int noOfProcessors_;			//for ATCA8020 there will be 4 processors, for ATCA8050 there will be 8 processors
		int noOfUsedProcessors_;		// Ho many processors are used by a EastFlexRunner i.e. how many instances are running on this blade and how many are free
		map <string,map<int,int> > reservedBy_;	//EastFlex will use this to understand who has conencted to this AdminListener, what package he is runnign and how many processors is being used. [<IP Addrss> <Package ID><Number of Test Case Instances>]
		vector <string> conenctedSystems_; //Who are connected to this AdminListener [IP Address]
		
		string isPowerHouseChassis_;	//If a Chassis is PowerHouse or HotWire Card attached to it
		int updateReservation(string eastFlexRunnerIP, int packageID, int numberofProcessor)
		{
		}
		int releaseReservation(string eastFlexRunnerIP, int packageID, int numberofProcessor)
		{
		}

	} sysInfo_;
	ConfigFileReader* loadConfigReader_;
	int runnerType_; //	This will help AdminListener to determine send spcific information like CPU Utilization/Memory Usage etc information along with KEEP_ALIVE response with out affecting the existing behavior
	void invokeEastFlexDesktop(AdminListenerConnectionPort*);
	int handleEastFlexGenerateIP(AdminListenerConnectionPort* admin);
	void clearErrorFDs(fd_set& errfds,int maxfds);
	void dumpStartnEndInfoToLogFile(int state/*1:Start, 2:STOP*/);
	int startRunner(string& port);
	int	stopRunner();
	int handleRESTCommand(AdminListenerConnectionPort* admin);
#if 0
	void handleStartSoftDataGenApplication(Notice* notice);
	void handleStopSoftDataGenApplication(Notice* notice);
#endif
	void handleStartApplication(Notice* notice, bool cpuShared = true);
	void handleStopApplication(Notice* notice, bool cpuShared = true);
	os_cardnum_t getPEOsCardNum(void);
	void loadCardNumber(void);
	void initRMInterface();
	void getHwListFromRM(void);
	void dumpRMLog(npg_rm::ResManNPGClient::ErrorCode errCode, string operation);
	//auto to_string(const npg_rm::ResManNPGClient::CardInfo& info);
	//auto to_string(const npg_rm::ResManNPGClient::PortInfo& info) ;
	void add2IPBucket(string startingIP,int ipType);
	void clearIPBucket(string devName,int maskVal, int ipType);
	void addIP2PrimaryBucket(string startingIP,int ipType);
	void addIP2SecondaryBucket(string startingIP,int ipType);
	void clearIPBucketSecondary(AdminListenerConnectionPort* admin,string devName,int maskVal, int ipType);
	void clearIPBuckets();
};
#endif

