#include "brain.h"
#include "brain_communication.h"

BrainCommunication::BrainCommunication(Brain *argBrain) : brain(argBrain)
{
}

BrainCommunication::~BrainCommunication()
{
    clearupGameControllerUnicast();
    clearupDiscoveryBroadcast();
    clearupDiscoveryReceiver();
    clearupCommunicationUnicast();
    clearupCommunicationReceiver();
}


void BrainCommunication::initCommunication()
{
    initGameControllerUnicast();
    if (brain->config->enableCom)
    {
        cout << RED_CODE << "队友通信已开启。" << RESET_CODE << endl;
        _discovery_udp_port = 20000 + brain->config->teamId;
        _unicast_udp_port = 30000 + brain->config->teamId;

        initDiscoveryBroadcast();
        initDiscoveryReceiver();
        initCommunicationUnicast();
        initCommunicationReceiver();
    }
    else
    {
        cout << RED_CODE << "队友通信已关闭。" << RESET_CODE << endl;
    }
}
    

void BrainCommunication::initGameControllerUnicast()
{
    try
    {
        _gc_send_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (_gc_send_socket < 0)
        {
        cout << RED_CODE << format("裁判机发送 socket 创建失败: %s", strerror(errno))
            << RESET_CODE << endl;
        throw std::runtime_error(strerror(errno));
        }
        // 配置目标地址
        string gamecontrol_ip = brain->get_parameter("game_control_ip").as_string();
        cout << GREEN_CODE << format("裁判机 IP: %s", gamecontrol_ip.c_str())
            << RESET_CODE << endl;
        _gcsaddr.sin_family = AF_INET;
        _gcsaddr.sin_addr.s_addr = inet_addr(gamecontrol_ip.c_str());
        _gcsaddr.sin_port = htons(GAMECONTROLLER_RETURN_PORT);

        _unicast_gamecontrol_flag = true;
        _gamecontrol_unicast_thread = std::thread([this](){ this->unicastToGameController(); });
    }
    catch(const std::exception& e)
    {
        std::cerr << "初始化裁判机回包发送失败: " << e.what() << '\n';
    }
}

void BrainCommunication::clearupGameControllerUnicast()
{
    _unicast_gamecontrol_flag = false;
    if (_gc_send_socket >= 0)
    {
        close(_gc_send_socket);
        _gc_send_socket = -1;
        cout << RED_CODE << format("裁判机发送 socket 已关闭。")
            << RESET_CODE << endl;
    }
    if (_gamecontrol_unicast_thread.joinable())
    {
        _gamecontrol_unicast_thread.join();
    }
}

void BrainCommunication::initDiscoveryBroadcast()
{
    try
    {
        _discovery_send_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (_discovery_send_socket < 0)
        {
            cout << RED_CODE << format("socket 创建失败: %s", strerror(errno))
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        // 设置广播选项
        int broadcast = 1;
        if (setsockopt(_discovery_send_socket, SOL_SOCKET, SO_BROADCAST, 
                    &broadcast, sizeof(broadcast)) < 0)
        {
            cout << RED_CODE << format("设置 SO_BROADCAST 失败: %s", strerror(errno))
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        // 配置广播地址
        _saddr.sin_family = AF_INET;
        _saddr.sin_addr.s_addr = INADDR_BROADCAST;  // 255.255.255.255
        _saddr.sin_port = htons(_discovery_udp_port);

        _broadcast_discovery_flag = true;
        _discovery_broadcast_thread = std::thread([this](){ this->broadcastDiscovery(); });
    }
    catch(const std::exception& e)
    {
        std::cerr << "初始化发现广播失败: " << e.what() << '\n';
        brain->log->log("error/communication", rerun::TextLog(format("初始化发现广播失败: %s", e.what())));
    }
    
}

void BrainCommunication::clearupDiscoveryBroadcast()
{
    _broadcast_discovery_flag = false;
    if (_discovery_send_socket >= 0)
    {
        close(_discovery_send_socket);
        _discovery_send_socket = -1;
        cout << RED_CODE << format("发现广播发送 socket 已关闭。")
            << RESET_CODE << endl;
    }

    if (_discovery_broadcast_thread.joinable())
    {
        _discovery_broadcast_thread.join();
    }
}

void BrainCommunication::initDiscoveryReceiver()
{
    try
    {
        _discovery_recv_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (_discovery_recv_socket < 0)
        {
            cout << RED_CODE << format("socket 创建失败: %s", strerror(errno))
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        // 允许地址重用
        int reuse = 1;
        if (setsockopt(_discovery_recv_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            cout << RED_CODE << format("设置 SO_REUSEADDR 失败: %s", strerror(errno))
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 接收所有网络接口的数据
        addr.sin_port = htons(_discovery_udp_port);
        
        if (bind(_discovery_recv_socket, (sockaddr *)&addr, sizeof(addr)) < 0)
        {
            cout << RED_CODE << format("绑定端口失败: %s (端口=%d)", strerror(errno), _discovery_udp_port)
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        cout << GREEN_CODE << format("正在监听 UDP 广播，端口 %d", _discovery_udp_port)
            << RESET_CODE << endl;

        _receive_discovery_flag = true;
        _discovery_recv_thread = std::thread([this](){ this->spinDiscoveryReceiver(); });
    }
    catch(const std::exception& e)
    {
        std::cerr << "初始化发现接收器失败: " << e.what() << '\n';
        brain->log->log("error/communication", rerun::TextLog(format("初始化发现接收器失败: %s", e.what())));
    }
}

void BrainCommunication::clearupDiscoveryReceiver()
{
    _receive_discovery_flag = false;
    if (_discovery_recv_socket >= 0)
    {
        close(_discovery_recv_socket);
        _discovery_recv_socket = -1;
        cout << RED_CODE << format("通信接收 socket 已关闭。")
            << RESET_CODE << endl;
    }
    if (_discovery_recv_thread.joinable())
    {
        _discovery_recv_thread.join();
    }
}

void BrainCommunication::unicastToGameController() {
    while (_unicast_gamecontrol_flag)
    {
        // cout << RED_CODE << format("unicastToGameController header=%s version=%d teamId=%d, playerId=%d", gc_return_data.header, gc_return_data.version, brain->config->teamId, brain->config->playerId)
        //     << RESET_CODE << endl;
        gc_return_data.team = brain->config->teamId;
        gc_return_data.player = brain->config->playerId; // return data 的id是1,2,3,4
        gc_return_data.message = GAMECONTROLLER_RETURN_MSG_ALIVE;

        int ret = sendto(_gc_send_socket, &gc_return_data, sizeof(gc_return_data), 0, (sockaddr *)&_gcsaddr, sizeof(_gcsaddr));
        if (ret < 0)
        {
            cout << RED_CODE << format("发送裁判机回包失败: %s", strerror(errno))
                << RESET_CODE << endl;
        }
        this_thread::sleep_for(chrono::milliseconds(BROADCAST_GAME_CONTROL_INTERVAL_MS));
    }
}

void BrainCommunication::broadcastDiscovery() {
    while (_broadcast_discovery_flag)
    {
        TeamDiscoveryMsg msg;

        msg.communicationId = _discovery_msg_id++;
        msg.teamId = brain->config->teamId;
        msg.playerId = brain->config->playerId;
        // // msg.playerRole = tree->getEntry<string>("player_role");
        // msg.isAlive = !brain->tree->getEntry<bool>("gc_is_under_penalty");
        // msg.ballDetected = brain->data->ballDetected;
        // msg.ballRange = brain->data->ball.range;
        // msg.ballPosToField = brain->data->ball.posToField;
        // msg.robotPoseToField = brain->data->robotPoseToField;

        int ret = sendto(_discovery_send_socket, &msg, sizeof(msg), 0, (sockaddr *)&_saddr, sizeof(_saddr));
        if (ret < 0)
        {
            cout << RED_CODE << format("发送发现广播失败: %s", strerror(errno))
                << RESET_CODE << endl;
        }
        // cout << GREEN_CODE << format("broadcastDiscovery: %d", msg.communicationId)
            // << RESET_CODE << endl;
        brain->data->discoveryMsgId = msg.communicationId;
        brain->data->discoveryMsgTime = brain->get_clock()->now();
        this_thread::sleep_for(chrono::milliseconds(BROADCAST_DISCOVERY_INTERVAL_MS));
    } 
}

void BrainCommunication::spinDiscoveryReceiver() {    
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);

    TeamDiscoveryMsg msg;

    while (_receive_discovery_flag) {

        ssize_t len = recvfrom(_discovery_recv_socket, &msg, sizeof(msg), 0, (sockaddr *)&addr, &addr_len);

        if (len < 0)
        {
            cout << RED_CODE << format("接收 UDP 消息失败: %s", strerror(errno))
                << RESET_CODE << endl;
            continue;
        }

        if (len != sizeof(TeamDiscoveryMsg)) {
            cout << YELLOW_CODE << format("收到 TeamDiscoveryMsg 数据包长度错误: %ld, 期望: %ld", len, sizeof(TeamDiscoveryMsg))
                << RESET_CODE << endl;
            continue;
        }

        if (msg.validation != VALIDATION_DISCOVERY) { // fail to pass validation
            cout << RED_CODE << format("收到 TeamDiscoveryMsg 数据包校验字段非法: %d", msg.validation)
                << RESET_CODE << endl;
            continue;
        } 

        if (msg.teamId != brain->config->teamId) { // 忽略其它队伍的消息
            cout << YELLOW_CODE << format("收到其它队伍的消息，队伍=%d，期望队伍=%d", msg.teamId, brain->config->teamId)
                << RESET_CODE << endl;
            continue;
        }

        if (msg.playerId == brain->config->playerId) {  // 忽略自己的消息
            // 处理自己的消息
            // cout << YELLOW_CODE <<  format(
            //     "discoveryID: %d, teamId:%d, playerId: %d, address: %s:%d",
            //     msg.communicationId, msg.teamId, msg.playerId, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port))
            //     << RESET_CODE << endl;
            continue;
        } else {
            // 处理队友消息
            // cout << GREEN_CODE <<  format(
            //     "discoveryID: %d, teamId:%d, playerId: %d, address: %s:%d",
            //     msg.communicationId, msg.teamId, msg.playerId, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port))
            //     << RESET_CODE << endl;
            
            auto time_now = brain->get_clock()->now();
            std::lock_guard<std::mutex> lock(_teammate_addresses_mutex);
            _teammate_addresses[addr.sin_addr.s_addr] = {
                addr.sin_addr.s_addr,
                msg.playerId,
                time_now
            };
        }
    }
}

void BrainCommunication::cleanupExpiredTeammates() {
    std::lock_guard<std::mutex> lock(_teammate_addresses_mutex);    
    for (auto it = _teammate_addresses.begin(); it != _teammate_addresses.end();) {
        auto timeDiff = this->brain->get_clock()->now().nanoseconds() - it->second.lastUpdate.nanoseconds();
        if (timeDiff > TEAMMATE_TIMEOUT_MS * 1e6) {
            cout << YELLOW_CODE << format("队友 %d 通信超时", it->second.playerId) 
                 << RESET_CODE << endl;
            it = _teammate_addresses.erase(it);
        } else {
            ++it;
        }
    }
}

void BrainCommunication::initCommunicationUnicast() {
    try
    {
        _unicast_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (_unicast_socket < 0) {
            cout << RED_CODE << format("创建单播发送 socket 失败: %s", strerror(errno))
                << RESET_CODE << endl;
            throw std::runtime_error("创建单播发送 socket 失败");
        }

        _unicast_saddr.sin_family = AF_INET;
        _unicast_saddr.sin_port = htons(_unicast_udp_port);

        _unicast_communication_flag = true;
        _unicast_thread = std::thread([this](){ this->unicastCommunication(); });
    }
    catch(const std::exception& e)
    {
        std::cerr << "初始化单播通信失败: " << e.what() << '\n';
        brain->log->log("error/communication", rerun::TextLog(format("初始化单播通信失败: %s", e.what())));
    }
    
}

void BrainCommunication::unicastCommunication() {
    auto log = [=](string msg) {
        brain->log->setTimeNow();
        brain->log->log("debug/sendMsg", rerun::TextLog(msg));
    };
    while (_unicast_communication_flag) {
        cleanupExpiredTeammates();
        TeamCommunicationMsg msg;
        msg.validation = VALIDATION_COMMUNICATION;
        msg.communicationId = _team_communication_msg_id++;
        msg.teamId = brain->config->teamId;
        msg.playerId = brain->config->playerId;
        msg.playerRole = brain->tree->getEntry<string>("player_role") == "striker" ? 1 : 2;
        msg.isAlive = brain->data->tmImAlive;
        msg.isLead = brain->data->tmImLead;
        msg.ballDetected = brain->data->ballDetected;
        msg.ballLocationKnown = brain->tree->getEntry<bool>("ball_location_known");
        msg.ballConfidence = brain->data->ball.confidence;
        msg.ballRange = brain->data->ball.range;
        msg.cost = brain->data->tmMyCost;
        msg.ballPosToField = brain->data->ball.posToField;
        msg.robotPoseToField = brain->data->robotPoseToField;
        msg.kickDir = brain->data->kickDir;
        msg.thetaRb = brain->data->robotBallAngleToField;
        msg.cmdId = brain->data->tmMyCmdId;
        msg.cmd = brain->data->tmMyCmd;
        log(format("ImAlive: %d, ImLead: %d, myCost: %.1f, myCmdId: %d, myCmd: %d", msg.isAlive, msg.isLead, msg.cost, msg.cmdId, msg.cmd));

        std::lock_guard<std::mutex> lock(_teammate_addresses_mutex);
        for (auto it = _teammate_addresses.begin(); it != _teammate_addresses.end(); ++it) {
            auto ip = it->second.ip;

            // cout << GREEN_CODE << format("unicastCommunication to %s", inet_ntoa(*(in_addr *)&ip))
            //     << RESET_CODE << endl;
            brain->data->tmIP = inet_ntoa(*(in_addr *)&ip);
            brain->data->sendId = msg.communicationId;
            brain->data->sendTime = brain->get_clock()->now();
            
            _unicast_saddr.sin_addr.s_addr = ip;
            int ret = sendto(_unicast_socket, &msg, sizeof(msg), 0, (sockaddr *)&_unicast_saddr, sizeof(_unicast_saddr));
            if (ret < 0) {
                cout << RED_CODE << format("发送队友单播消息失败: %s", strerror(errno))
                    << RESET_CODE << endl;
            }
        }
        this_thread::sleep_for(chrono::milliseconds(UNICAST_INTERVAL_MS));
    }
}

void BrainCommunication::clearupCommunicationUnicast() {
    _unicast_communication_flag = false;
    if (_unicast_socket >= 0) {
        close(_unicast_socket);
        _unicast_socket = -1;
        cout << RED_CODE << format("通信发送 socket 已关闭。")
            << RESET_CODE << endl;
    }

    if (_unicast_thread.joinable()) {
        _unicast_thread.join();
    }
}

void BrainCommunication::initCommunicationReceiver() {
    try
    {
        _communication_recv_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (_communication_recv_socket < 0) {
            cout << RED_CODE << format("创建通信接收 socket 失败: %s", strerror(errno))
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        // 允许地址重用
        int reuse = 1;
        if (setsockopt(_communication_recv_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            cout << RED_CODE << format("设置 SO_REUSEADDR 失败: %s", strerror(errno))
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(_unicast_udp_port);
        
        if (bind(_communication_recv_socket, (sockaddr *)&addr, sizeof(addr)) < 0) {
            cout << RED_CODE << format("绑定端口失败: %s (端口=%d)", strerror(errno), _unicast_udp_port)
                << RESET_CODE << endl;
            throw std::runtime_error(strerror(errno));
        }

        _receive_communication_flag = true;
        _communication_recv_thread = std::thread([this](){ this->spinCommunicationReceiver(); });
    
    }
    catch(const std::exception& e)
    {
        std::cerr << "初始化通信接收器失败: " << e.what() << '\n';
        brain->log->log("error/communication", rerun::TextLog(format("初始化通信接收器失败: %s", e.what())));
    }
}

void BrainCommunication::spinCommunicationReceiver() {
    auto log = [=](string msg) {
        brain->log->setTimeNow();
        brain->log->log("debug/receiveMsg", rerun::TextLog(msg));
    };
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);

    TeamCommunicationMsg msg;

    while (_receive_communication_flag) {

        ssize_t len = recvfrom(_communication_recv_socket, &msg, sizeof(msg), 0, (sockaddr *)&addr, &addr_len);

        if (len < 0) {
            cout << RED_CODE << format("接收 UDP 消息失败: %s", strerror(errno))
                << RESET_CODE << endl;
            continue;
        }

        if (len != sizeof(TeamCommunicationMsg)) {
            cout << YELLOW_CODE << format("收到 TeamCommunicationMsg 数据包长度错误: %ld, 期望: %ld", len, sizeof(TeamCommunicationMsg))
                << RESET_CODE << endl;
            continue;
        }

        if (msg.validation != VALIDATION_COMMUNICATION) { // fail to pass validation
            cout << RED_CODE << format("收到 TeamCommunicationMsg 数据包校验字段非法: %d", msg.validation)
                << RESET_CODE << endl;
            continue;
        }

        if (msg.teamId != brain->config->teamId) { // 忽略其它队伍的消息
            cout << YELLOW_CODE << format("收到其它队伍的消息，队伍=%d，期望队伍=%d", msg.teamId, brain->config->teamId)
                << RESET_CODE << endl;
            continue;
        }

        if (msg.playerId == brain->config->playerId) {  // 忽略自己的消息
            // 处理自己的消息
            cout << CYAN_CODE <<  format(
                "通信ID: %d, 存活: %d, 看到球: %d, 球距离: %.2f, 球员ID: %d",
                msg.communicationId, msg.isAlive, msg.ballDetected, msg.ballRange, msg.playerId)
                << RESET_CODE << endl;
            brain->data->sendId = msg.communicationId;
            brain->data->sendTime = brain->get_clock()->now();
            continue;
        } 

        // else 处理队友消息
        // cout << GREEN_CODE <<  format(
        //     "communicationId: %d, alive: %d, ballDetected: %d ballRange: %.2f playerId: %d",
        //     msg.communicationId, msg.isAlive, msg.ballDetected, msg.ballRange, msg.playerId)
        //     << RESET_CODE << endl;
        auto tmIdx = msg.playerId - 1;

        if (tmIdx < 0 || tmIdx >= HL_MAX_NUM_PLAYERS) { // HL_MAX_NUM_PLAYERS 是最大球员数
            cout << YELLOW_CODE << format("收到非法球员 ID 的通信消息: %d", msg.playerId) << RESET_CODE << endl;
            continue;
        }

        if (brain->data->penalty[tmIdx] == SUBSTITUTE) { // 不处理替补队员的信息
            cout << YELLOW_CODE << format("球员 %d 是替补队员，忽略通信消息", msg.playerId) << RESET_CODE << endl;
            continue;
        }

        log(format("队友ID: %.d, 存活: %d, 主控: %d, 成本: %.1f, 指令ID: %d, 指令: %d", msg.playerId, msg.isAlive, msg.isLead, msg.cost, msg.cmdId, msg.cmd));

        TMStatus &tmStatus = brain->data->tmStatus[tmIdx];
        
        tmStatus.role = msg.playerRole == 1 ? "striker" : "goal_keeper";
        tmStatus.isAlive = msg.isAlive;
        tmStatus.ballDetected = msg.ballDetected;
        tmStatus.ballLocationKnown = msg.ballLocationKnown;
        tmStatus.ballConfidence = msg.ballConfidence;
        tmStatus.ballRange = msg.ballRange;
        tmStatus.cost = msg.cost;
        tmStatus.isLead = msg.isLead;
        tmStatus.ballPosToField = msg.ballPosToField;
        tmStatus.robotPoseToField = msg.robotPoseToField;
        tmStatus.kickDir = msg.kickDir;
        tmStatus.thetaRb = msg.thetaRb;
        tmStatus.timeLastCom = brain->get_clock()->now();
        tmStatus.cmd = msg.cmd;
        tmStatus.cmdId = msg.cmdId;

        // 检查是否收到了新的指令
        if (msg.cmdId > brain->data->tmCmdId) {
            brain->data->tmCmdId = msg.cmdId;
            brain->data->tmReceivedCmd = msg.cmd;
            brain->data->tmLastCmdChangeTime = brain->get_clock()->now();
            log(format("收到队友 %d 的新指令: %d", msg.playerId, msg.cmd));
        }

    }
}

void BrainCommunication::clearupCommunicationReceiver() {
    _receive_communication_flag = false;
    if (_communication_recv_socket >= 0) {
        close(_communication_recv_socket);
        _communication_recv_socket = -1;
        cout << RED_CODE << format("通信接收 socket 已关闭。")
            << RESET_CODE << endl;
    }
    if (_communication_recv_thread.joinable()) {
        _communication_recv_thread.join();
    }
}
