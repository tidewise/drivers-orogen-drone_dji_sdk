/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "DroneFlightControlTask.hpp"

using namespace DJI::OSDK;
using namespace drone_dji_sdk;
using namespace VehicleStatus;

DroneFlightControlTask::DroneFlightControlTask(std::string const &name)
    : DroneFlightControlTaskBase(name)
{
}

DroneFlightControlTask::~DroneFlightControlTask()
{
}

/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See DroneFlightControlTask.hpp for more detailed
// documentation about them.

bool DroneFlightControlTask::configureHook()
{
    if (!DroneFlightControlTaskBase::configureHook())
        return false;

    // Init class members
    mFunctionTimeout = 1; // second
    mStatusFreqInHz = 10; // Hz

    // Configure GPS stuffs
    mGPSSolution.setParameters(_utm_parameters.get());

    if (!initVehicle())
        return false;
    if (!checkTelemetrySubscription())
        return false;

    return true;
}

bool DroneFlightControlTask::startHook()
{
    if (!DroneFlightControlTaskBase::startHook())
        return false;

    // Obtain Control Authority
    mVehicle->obtainCtrlAuthority(mFunctionTimeout);
    return true;
}

typedef DroneFlightControlTask::States TaskState;
static TaskState djiStatusFlightToTaskState(uint8_t status)
{
    switch (status)
    {
    case DJI::OSDK::VehicleStatus::FlightStatus::STOPED:
        return TaskState::DJI_STOPPED;
    case DJI::OSDK::VehicleStatus::FlightStatus::ON_GROUND:
        return TaskState::DJI_ON_GROUND;
    case DJI::OSDK::VehicleStatus::FlightStatus::IN_AIR:
        return TaskState::DJI_IN_AIR;
    }
    // Never reached
    throw std::invalid_argument("invalid controller state");
}

void DroneFlightControlTask::updateHook()
{
    _pose_samples.write(getRigidBodyState());
    _battery.write(getBatteryStatus());

        // Check status
    auto djiStatusFlight = mVehicle->subscribe->getValue<Telemetry::TOPIC_STATUS_FLIGHT>();
    DroneFlightControlTask::States status = djiStatusFlightToTaskState(djiStatusFlight);
    if (state() != status)
        state(status);

    // cmd input
    if (_cmd_input.read(mCmdInput) == RTT::NoData)
        return;

    if (djiStatusFlight != VehicleStatus::FlightStatus::IN_AIR && mCmdInput != TAKEOFF_ACTIVATE)
    {
        return;
    }

    switch (mCmdInput)
    {
    case TAKEOFF_ACTIVATE:
        return takeoff();
    case LANDING_ACTIVATE:
        return land();
    case GO_TO_ACTIVATE:
        return goTo();
    case MISSION_ACTIVATE:
        return mission();
    }

    DroneFlightControlTaskBase::updateHook();
}
void DroneFlightControlTask::errorHook()
{
    DroneFlightControlTaskBase::errorHook();
}
void DroneFlightControlTask::stopHook()
{
    mVehicle->releaseCtrlAuthority(mFunctionTimeout);

    DroneFlightControlTaskBase::stopHook();
}
void DroneFlightControlTask::cleanupHook()
{
    int pkgIndex = 0;
    /*! Telemetry subscription*/
    if (!teardownSubscription(pkgIndex))
        DERROR("Failed to tear down Subscription!");

    DroneFlightControlTaskBase::cleanupHook();
}

bool DroneFlightControlTask::initVehicle()
{
    bool threadSupport = true;
    bool useAdvancedSensing = false;
    std::unique_ptr<Vehicle> vehicle(new Vehicle(_device.get().c_str(),
                                                 _baudrate.get(),
                                                 threadSupport,
                                                 useAdvancedSensing));

    // Check if the communication is working fine
    if (!vehicle->protocolLayer->getDriver()->getDeviceStatus())
    {
        DERROR("Comms appear to be incorrectly set up. Exiting.");
        return false;
    }

    // Activate
    mActivateData.ID = _app_id.get();
    char app_key[65];
    mActivateData.encKey = app_key;
    strcpy(mActivateData.encKey, _app_key.get().c_str());
    mActivateData.version = vehicle->getFwVersion();
    ACK::ErrorCode ack = vehicle->activate(&mActivateData, mFunctionTimeout);

    if (ACK::getError(ack))
    {
        ACK::getErrorCodeMessage(ack, __func__);
        return false;
    }

    mVehicle = std::move(vehicle);
    return true;
}

bool DroneFlightControlTask::checkTelemetrySubscription()
{
    /*! Verify and setup the subscription */
    // Status flight, status display mode, Topic quaternion and GPS fused
    const int pkgIndex = 0;
    std::vector<Telemetry::TopicName> topicList = {Telemetry::TOPIC_STATUS_FLIGHT, Telemetry::TOPIC_STATUS_DISPLAYMODE,
                                                   Telemetry::TOPIC_QUATERNION, Telemetry::TOPIC_GPS_FUSED};
    if (!setUpSubscription(pkgIndex, mStatusFreqInHz, topicList))
        return false;

    /*! wait for subscription data come*/
    sleep(mFunctionTimeout);
    return true;
}

bool DroneFlightControlTask::missionInitSettings(Mission mission)
{
    // Waypoint Mission : Initialization
    WayPointInitSettings fdata = getWaypointInitDefaults(mission);

    ACK::ErrorCode initAck = mVehicle->missionManager->init(
        DJI_MISSION_TYPE::WAYPOINT, mFunctionTimeout, &fdata);
    if (ACK::getError(initAck))
    {
        ACK::getErrorCodeMessage(initAck, __func__);
        return false;
    }

    mVehicle->missionManager->printInfo();
    return true;
}

void DroneFlightControlTask::takeoff()
{
    // setpoint input
    if (_cmd_pos.read(mCmdPos) != RTT::NewData)
        return;

    auto djiStatusFlight = mVehicle->subscribe->getValue<Telemetry::TOPIC_STATUS_FLIGHT>();
    auto djiDisplayMode = mVehicle->subscribe->getValue<Telemetry::TOPIC_STATUS_DISPLAYMODE>();

    if (djiDisplayMode == DisplayMode::MODE_ASSISTED_TAKEOFF ||
        djiDisplayMode == DisplayMode::MODE_AUTO_TAKEOFF)
    {
        return;
    }

    if (djiStatusFlight == VehicleStatus::FlightStatus::IN_AIR)
    {
        goTo();
        return;
    }

    mVehicle->control->takeoff(mFunctionTimeout);
}

void DroneFlightControlTask::land()
{
    auto djiDisplayMode = mVehicle->subscribe->getValue<Telemetry::TOPIC_STATUS_DISPLAYMODE>();

    if (djiDisplayMode == DisplayMode::MODE_AUTO_LANDING ||
        djiDisplayMode == DisplayMode::MODE_FORCE_AUTO_LANDING)
    {
        return;
    }

    mVehicle->control->land(mFunctionTimeout);
}

void DroneFlightControlTask::goTo()
{
    if(mCmdInput == GO_TO_ACTIVATE)
    {
        // setpoint input
        if (_cmd_pos.read(mCmdPos) != RTT::NewData)
            return;
    }

    // get the vector between aircraft and target point.
    base::Vector3d offset = (mCmdPos.position - getRigidBodyState().position);
    // get the orientation between aircraft and target - in Deg!
    float yawInRad = base::getYaw(getRigidBodyState().orientation);
    float yawDesiredInDeg = (mCmdPos.heading.rad - yawInRad) * 180 / M_PI;

    float32_t xCmd = static_cast<float>(offset[0]);
    float32_t yCmd = static_cast<float>(offset[1]);
    float32_t zCmd = static_cast<float>(mCmdPos.position[2]) +
                                        static_cast<float>(getRigidBodyState().position[2]);

    mVehicle->control->positionAndYawCtrl(xCmd, yCmd, zCmd, yawDesiredInDeg);
}

void DroneFlightControlTask::mission()
{
    // waypoint input
    Mission mission;
    if (_cmd_mission.read(mission) != RTT::NewData)
        return;

    // Config mission
    if (!missionInitSettings(mission))
        return;

    for (unsigned int i = 0; i < mission.waypoints.size(); i++)
    {
        WayPointSettings wpp = getWaypointSettings(mission.waypoints[i], i);
        ACK::WayPointIndex wpDataACK =
            mVehicle->missionManager->wpMission->uploadIndexData(&wpp,
                                                                 mFunctionTimeout);
        ACK::getErrorCodeMessage(wpDataACK.ack, __func__);
    }

    // Waypoint Mission: Start
    ACK::ErrorCode startAck =
        mVehicle->missionManager->wpMission->start(mFunctionTimeout);
    if (ACK::getError(startAck))
    {
        ACK::getErrorCodeMessage(startAck, __func__);
    }
    else
    {
        DSTATUS("Starting Waypoint Mission..");
    }
}

WayPointInitSettings DroneFlightControlTask::getWaypointInitDefaults(Mission mission)
{
    WayPointInitSettings fdata;
    fdata.indexNumber = 2;
    fdata.maxVelocity = mission.max_velocity;
    fdata.idleVelocity = mission.idle_velocity;
    fdata.finishAction = mission.finish_action;
    fdata.executiveTimes = mission.executive_times;
    fdata.yawMode = mission.yaw_mode;
    fdata.traceMode = mission.trace_mode;
    fdata.RCLostAction = mission.rc_lost_action;
    fdata.gimbalPitch = mission.gimbal_pitch;
    fdata.latitude = mission.latitude.rad;
    fdata.longitude = mission.longitude.rad;
    fdata.altitude = mission.altitude;

    return fdata;
}

WayPointSettings DroneFlightControlTask::getWaypointSettings(Waypoint cmd_waypoint, int index)
{
    WayPointSettings wp;
    wp.index = index;
    wp.latitude = cmd_waypoint.latitude.rad;
    wp.longitude = cmd_waypoint.longitude.rad;
    wp.altitude = cmd_waypoint.altitude;
    wp.damping = cmd_waypoint.damping;
    wp.yaw = cmd_waypoint.yaw.rad;
    wp.gimbalPitch = cmd_waypoint.gimbal_pitch.rad;
    wp.turnMode = cmd_waypoint.turn_mode;
    for (int i = 0; i < 8; i++)
        wp.reserved[i] = 0;
    if (cmd_waypoint.actions.empty())
        wp.hasAction = 0;
    else
        wp.hasAction = 1;
    wp.actionTimeLimit = 100;
    wp.actionNumber = cmd_waypoint.actions.size();
    wp.actionRepeat = 0;
    for (int i = 0; i < 16; i++)
    {
        wp.commandList[i] = cmd_waypoint.actions[i].command;
        wp.commandParameter[i] = cmd_waypoint.actions[i].command_parameter;
    }
    return wp;
}

power_base::BatteryStatus DroneFlightControlTask::getBatteryStatus() const
{
    auto djiBattery = mVehicle->broadcast->getBatteryInfo();

    power_base::BatteryStatus battery;
    battery.time = base::Time::fromMilliseconds(mVehicle->broadcast->getTimeStamp().time_ms);
    battery.current = djiBattery.current;
    battery.voltage = djiBattery.voltage;
    battery.charge = static_cast<float>(djiBattery.percentage) / 100;
    return battery;
}

base::samples::RigidBodyState DroneFlightControlTask::getRigidBodyState() const
{
    base::samples::RigidBodyState cmd;
    cmd.time = base::Time::fromMilliseconds(mVehicle->broadcast->getTimeStamp().time_ms);
    cmd.orientation.w() = mVehicle->broadcast->getQuaternion().q0;
    cmd.orientation.x() = mVehicle->broadcast->getQuaternion().q1;
    cmd.orientation.y() = mVehicle->broadcast->getQuaternion().q2;
    cmd.orientation.z() = mVehicle->broadcast->getQuaternion().q3;
    cmd.velocity.x() = mVehicle->broadcast->getVelocity().x;
    cmd.velocity.y() = mVehicle->broadcast->getVelocity().y;
    cmd.velocity.z() = mVehicle->broadcast->getVelocity().z;
    cmd.angular_velocity.x() = mVehicle->broadcast->getAngularRate().x;
    cmd.angular_velocity.y() = mVehicle->broadcast->getAngularRate().y;
    cmd.angular_velocity.z() = mVehicle->broadcast->getAngularRate().z;
    // Get GPS position information
    Telemetry::GlobalPosition gpsInfo = mVehicle->broadcast->getGlobalPosition();
    gps_base::Solution solution;
    solution.time = cmd.time;
    solution.latitude = gpsInfo.latitude * 180 / M_PI;
    solution.longitude = gpsInfo.longitude * 180 / M_PI;
    solution.altitude = gpsInfo.altitude / 1000;
    // Convert position data from GPS to NWU
    base::samples::RigidBodyState gpsPosition = mGPSSolution.convertToNWU(solution);
    // Convert to NEU
    cmd.position = gpsPosition.position;
    cmd.position[1] = -gpsPosition.position[1];

    return cmd;
}

bool DroneFlightControlTask::setUpSubscription(int pkgIndex, int freq,
                                               std::vector<Telemetry::TopicName> topicList)
{
    if (!mVehicle)
    {
        DERROR("vehicle haven't been initialized", __func__);
        return false;
    }
    /*! Telemetry: Verify the subscription*/
    ACK::ErrorCode subscribeStatus =
        mVehicle->subscribe->verify(mFunctionTimeout);
    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
        ACK::getErrorCodeMessage(subscribeStatus, __func__);
        return false;
    }

    bool enableTimestamp = false;
    bool pkgStatus = mVehicle->subscribe->initPackageFromTopicList(
        pkgIndex, topicList.size(), topicList.data(), enableTimestamp, freq);
    if (!(pkgStatus))
        return false;

    /*! Start listening to the telemetry data */
    subscribeStatus = mVehicle->subscribe->startPackage(pkgIndex, mFunctionTimeout);

    if (ACK::getError(subscribeStatus) != ACK::SUCCESS)
    {
        ACK::getErrorCodeMessage(subscribeStatus, __func__);
        /*! Cleanup*/
        ACK::ErrorCode ack = mVehicle->subscribe->removePackage(pkgIndex, mFunctionTimeout);
        if (ACK::getError(ack))
        {
            DERROR("Error unsubscription; please restart the drone/FC to get "
                   "back to a clean state");
            throw std::invalid_argument("Error unsubscription");
        }
        return false;
    }
    return true;
}

bool DroneFlightControlTask::teardownSubscription(const int pkgIndex)
{
    ACK::ErrorCode ack = mVehicle->subscribe->removePackage(pkgIndex, mFunctionTimeout);
    if (ACK::getError(ack))
    {
        DERROR(
            "Error unsubscription; please restart the drone/FC to get back "
            "to a clean state.");
        return false;
    }
    return true;
}
