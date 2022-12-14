//
// Created by vlad on 05.02.2021.
//
#include <ros/ros.h>
#include <ros/console.h>

#include <std_msgs/Bool.h>
#include <std_msgs/String.h>
#include <tf2_msgs/TFMessage.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <cds_msgs/PathStamped.h>
#include <geometry_msgs/PoseArray.h>
#include <nav_msgs/Odometry.h>
#include "visualization_msgs/Marker.h"

#include "planner_cds_core/mission.h"

#include "planner_cds/utils.h"
#include "planner_cds/const.h"

#include <chrono>


class Planner{
private:
    ros::Subscriber     taskSub;
    ros::Subscriber     gridSub;
    ros::Subscriber     replanSub;
    ros::Subscriber     controlStatusSub;
    ros::Publisher      trajPub;
    ros::Publisher      visTrajPub;
    ros::Publisher      plannerStatusPub;
//    ros::Subscriber     odomSub;

    tf2_ros::Buffer&                tfBuffer;

    nav_msgs::Odometry              odom;
    geometry_msgs::PoseStamped      goal;
    geometry_msgs::Pose             transformedGoal;
    nav_msgs::OccupancyGrid         grid;
    visualization_msgs::Marker      vis_path;

    Map         map;
    SearchResult searchRes;

    ISearch* search = nullptr;


    std::string taskTopic;
    std::string gridTopic;
    std::string pathTopic;
    std::string pathTopicVis;

    std::string searchType;
    std::string globalFrame;
    std::string baseFrame;

    bool gridSet = false;
    bool taskSet = false;
    geometry_msgs::PoseArray path;
public:
    Planner(tf2_ros::Buffer& _tfBuffer);
    void setTask(const geometry_msgs::PoseStamped::ConstPtr& goalMsg);
    void setGrid(const nav_msgs::OccupancyGrid::ConstPtr& gridMsg);
    void needReplan(const std_msgs::Bool::ConstPtr& boolMsg);
    void callBackStatus(const std_msgs::String::ConstPtr& statusMsg);

    void getRobotPose();
//    void getRobotPose(const nav_msgs::Odometry::ConstPtr& odomMsg);
    bool plan();
    void replan(bool isCurrentPathFeasible);
    bool checkPath();
    void fillPath(std::list<Node> nodePath);
    void fillPathVis();
    void transformPath();
    void transformPathBack();
    void publish();
    bool freeRobot();

    geometry_msgs::Pose transformPoseToTargetFrame(geometry_msgs::Pose poseIn, std::string poseFrame, std::string targetFrame);
    geometry_msgs::Pose rescaleToGrid(geometry_msgs::Pose Pose);
    geometry_msgs::Pose rescaleFromGrid(geometry_msgs::Pose Pose);

};

Planner::Planner(tf2_ros::Buffer& _tfBuffer): tfBuffer(_tfBuffer){
    ros::NodeHandle nh;


    nh.param<std::string>("/node_params/task_topic", taskTopic, "some_task");
    nh.param<std::string>("/node_params/grid_topic", gridTopic, "some_grid");
    nh.param<std::string>("/node_params/path_topic", pathTopic, "some_path");
    nh.param<std::string>("/node_params/path_topic_vis", pathTopicVis, "some_path");

    nh.param<std::string>("/node_params/search_type", searchType, "dijkstra");
    nh.param<std::string>("/node_params/global_frame", globalFrame, "map");
    nh.param<std::string>("/node_params/base_frame", baseFrame, "base_link");


    vis_path.ns = "planning";
    vis_path.id = 0;
    vis_path.type = 4;
    vis_path.action = 0;
    vis_path.pose.position.x = 0;
    vis_path.pose.position.y = 0;
    vis_path.pose.position.z = 0;
    vis_path.pose.orientation.x = 0;
    vis_path.pose.orientation.y = 0;
    vis_path.pose.orientation.z = 0;
    vis_path.pose.orientation.w = 1;
    vis_path.scale.x = 0.05;
    vis_path.scale.y = 0.05;
    vis_path.scale.z = 0;
    vis_path.color.r = 1;
    vis_path.color.g = 0;
    vis_path.color.b = 0;
    vis_path.color.a = 1;
    vis_path.lifetime.sec = 0;
    vis_path.lifetime.nsec = 0;
    vis_path.frame_locked = false;
    vis_path.colors.clear();
    vis_path.text = "";
    vis_path.mesh_resource = "";
    vis_path.mesh_use_embedded_materials = false;

    taskSub                     = nh.subscribe<geometry_msgs::PoseStamped>      (taskTopic,
                                                                                50,
                                                                                &Planner::setTask,
                                                                                this);

    gridSub                     = nh.subscribe<nav_msgs::OccupancyGrid>         (gridTopic,
                                                                                50,
                                                                                &Planner::setGrid,
                                                                                this);

    replanSub                   = nh.subscribe<std_msgs::Bool>                 ("replan",
                                                                                 50,
                                                                                 &Planner::needReplan,
                                                                                 this);

//    odomSub                     = nh.subscribe<nav_msgs::Odometry>             ("lol_odom",
//                                                                                 50,
//                                                                                 &Planner::getRobotPose,
//                                                                                 this);
    controlStatusSub            = nh.subscribe<std_msgs::String>                ("control_status",
                                                                                 50,
                                                                                 &Planner::callBackStatus,
                                                                                 this);

    plannerStatusPub            = nh.advertise<std_msgs::String>                ("planner_status",
                                                                                 50);

    trajPub                     = nh.advertise<geometry_msgs::PoseArray>        (pathTopic,
                                                                                50);

    visTrajPub                  = nh.advertise<visualization_msgs::Marker>      (pathTopicVis,
                                                                                 50);



    if (search)
        delete search;
    if (searchType == PT_BFS)
        search = new BFS();
    else if (searchType  == PT_DIJK)
        search = new Dijkstra();
    else if (searchType  == PT_ASTAR)
        search = new Astar(1, 1);
    else if (searchType  == PT_JP)
        search = new JP_Search(1, 1);
    else if (searchType  == PT_TH)
        search = new Theta(1, 1);
}

void Planner::setTask(const geometry_msgs::PoseStamped::ConstPtr& goalMsg) {
    taskSet = true;
    this->goal = *goalMsg;
//    plan();
//    publish();
    if(gridSet){
        if(!plan()) ROS_WARN_STREAM("Planning error! Resulted path is empty.");
        publish();
        ROS_INFO_STREAM("Going to plan");
    }else{
        ROS_WARN_STREAM("No grid received! Cannot set task.");
    }
}

void Planner::setGrid(const nav_msgs::OccupancyGrid::ConstPtr& gridMsg) {
//    gridSet = true;
    if (gridMsg->data.size() != 0) {
        this->grid = *gridMsg;
        if (map.getMap(gridMsg)) {
            gridSet = true;
            if (taskSet) replan(checkPath());
        } else {
            gridSet = false;
            ROS_WARN_STREAM("Cannot set map!");
        }
    } else {
        gridSet = false;
        ROS_WARN_STREAM("Received map is empty!");
    }
}


void Planner::needReplan(const std_msgs::Bool::ConstPtr& boolMsg){
//    ROS_INFO_STREAM("Calling specific function");
        if(boolMsg->data){
            this->replan(false);
        }
    }

void Planner::callBackStatus(const std_msgs::String::ConstPtr& statusMsg){
    if (statusMsg->data == "reached"){
        std_msgs::String status;
        status.data = "reached";
        plannerStatusPub.publish(status);
    }else if (statusMsg->data == "replan"){
        this->replan(false);
    }
}

void Planner::getRobotPose() {
    geometry_msgs::TransformStamped transform;
    try {
        transform = tfBuffer.lookupTransform(globalFrame, baseFrame, ros::Time(0));
    }catch (tf2::TransformException &ex) {
        ROS_WARN("%s", ex.what());
    }
    this->odom.header.frame_id = globalFrame;
    this->odom.child_frame_id = baseFrame;
    this->odom.pose.pose.position.x = transform.transform.translation.x;
    this->odom.pose.pose.position.y = transform.transform.translation.y;
    this->odom.pose.pose.position.z = transform.transform.translation.z;
    this->odom.pose.pose.orientation = transform.transform.rotation;
}

//void Planner::getRobotPose(const nav_msgs::Odometry::ConstPtr& odomMsg) {
//    this->odom = *odomMsg;
//}


void Planner::replan(bool isCurrentPathFeasible){
    auto old_searchRes = searchRes;

    /////////////////////////////////
    getRobotPose();
    /////////////////////////////////

//    ROS_INFO_STREAM("Replanning with current path: " << (isCurrentPathFeasible ? "Correct" : "Incorrect"));
    if(!isCurrentPathFeasible){
        ROS_INFO_STREAM("Replanning");
        bool planSuccess = plan();
        if(!planSuccess) {
            //Current path is wrong and replanning error
            ROS_ERROR_STREAM("Replanning error! Sending empty path");
            path.poses.clear();
            vis_path.points.clear();
            publish();
        }else {
            //Current path is wrong but replanning succeeded
            ROS_INFO_STREAM("Replanning succeeded! Sending new path");
            auto nodePath = searchRes.hppath;
            fillPath(*nodePath);
            transformPath();
            fillPathVis();
            publish();
        }
    }
}
bool Planner::plan() {
    XmlLogger *logger = new XmlLogger("nope");
    const EnvironmentOptions options;

    /////////////////////////////////////
    getRobotPose();
    /////////////////////////////////////

    transformedGoal = rescaleToGrid(transformPoseToTargetFrame(goal.pose, goal.header.frame_id, grid.header.frame_id));

    if(!map.CellOnGrid(transformedGoal.position.y, transformedGoal.position.x)){
        ROS_WARN_STREAM("Goal point is below map. Changing to nearest map point.");
        transformedGoal.position.y = (transformedGoal.position.y < 0 ? 0 : transformedGoal.position.y);
        transformedGoal.position.x = (transformedGoal.position.x < 0 ? 0 : transformedGoal.position.x);

        transformedGoal.position.y = (transformedGoal.position.y >= map.height ? map.height-1 : transformedGoal.position.y);
        transformedGoal.position.x = (transformedGoal.position.x >= map.width ? map.width-1 : transformedGoal.position.x);
    }



    map.setGoal(int(transformedGoal.position.y),
                int(transformedGoal.position.x));

    auto transformedOdom = rescaleToGrid(transformPoseToTargetFrame(odom.pose.pose, odom.header.frame_id, grid.header.frame_id));
    map.setStart(int(transformedOdom.position.y),
                 int(transformedOdom.position.x));


    if(map.CellIsObstacle(map.start_i, map.start_j)){
        ROS_WARN_STREAM("Start pose is dangerously close to obstacle. Pretending to find a way to free space.");
        if(!freeRobot()){
            ROS_ERROR_STREAM("Wrong start coordinates! Interrupting");
            return false;
        }
    }

    ROS_INFO_STREAM("Start position is: " << map.start_i << "  " << map.start_j);
    ROS_INFO_STREAM("Goal position is: " << map.goal_i << "  " << map.goal_j);

    if((map.start_j > map.width) || (map.start_i > map.height) || (map.start_j < 0) || (map.start_i < 0)){
        ROS_WARN_STREAM("Wrong start coordinates! Interrupting");
        return false;
    }

    if((map.goal_j > map.width) || (map.goal_i > map.height) || (map.goal_j < 0) || (map.goal_i < 0)){
        ROS_WARN_STREAM("Wrong goal coordinates! Interrupting");
        return false;
    }

    if (map.CellIsWall(map.start_i, map.start_j)) {
        ROS_WARN_STREAM("Error! Start cell is not traversable. Interrupting");
        return false;
    }

    if (map.CellIsObstacle(map.goal_i, map.goal_j)) {
        ROS_WARN_STREAM("Error! Goal cell is not traversable. Interrupting");
        return false;
    }

    ROS_INFO_STREAM("Starting search");
    searchRes = search->startSearch(logger, map, options);
    ROS_INFO_STREAM("Planning time: " << searchRes.time);

    if (searchRes.hppath->size() == 0) return false;



    auto nodePath = searchRes.hppath;

    fillPath(*nodePath);
    transformPath();
    fillPathVis();
    if(logger) delete logger;
    return true;
}

bool Planner::checkPath() {
    if(!map.CellOnGrid(transformedGoal.position.y, transformedGoal.position.x)) {ROS_WARN_STREAM("Goal is not on grid"); return false;}

    if (map.CellIsObstacle(map.goal_i, map.goal_j)) {
        ROS_WARN_STREAM("Error! Goal cell is not traversable. Replanning");
        return false;
    }

    transformPathBack();
    if (path.poses.size() == 0) {ROS_WARN_STREAM("Empty path"); transformPath(); return false;}


    for(int i = 0; i < path.poses.size()-1; ++i){
        auto point1 = path.poses[i];
        auto point2 = path.poses[i+1];
        auto dy = point2.position.y - point1.position.y;
        auto dx = point2.position.x - point1.position.x;
        auto stepX = dx > 0 ? 1 : -1;
        auto stepY = dy > 0 ? 1 : -1;

        if(dy == 0){

            for(int s = 0; s < abs(dx); ++s){
                if(grid.data[point1.position.y*grid.info.width + point1.position.x + s*stepX] > 50){
                    ROS_WARN_STREAM("Path is not feasible. Error on point: " +
                                    std::to_string(point1.position.y) + " " +
                                    std::to_string(point1.position.x));
//                    ROS_WARN_STREAM("1");
                    transformPath();
                    return false;
                }
            }
            continue;
        }

        if(dx == 0){

            for(int s = 0; s < abs(dy); ++s){
                if(grid.data[(point1.position.y + s*stepY)*grid.info.width + point1.position.x] > 50){
                    ROS_WARN_STREAM("Path is not feasible. Error on point: " +
                                    std::to_string(point1.position.y) + " " +
                                    std::to_string(point1.position.x));
//                    ROS_WARN_STREAM("2");
                    transformPath();
                    return false;}
            }
            continue;
        }
//        ROS_INFO_STREAM(dx << dy);
        auto total_steps = std::max(abs(dy), abs(dx));

        stepX = dx / total_steps;
        stepY = dy / total_steps;
//        ROS_INFO_STREAM("Total steps: " << total_steps);
        for(int s = 0; s < total_steps; ++s){
//            ROS_INFO_STREAM(int(point1.position.y + s*stepY)<< "  " << int(point1.position.x + s*stepX));
            if(grid.data[int(point1.position.y + s*stepY)*grid.info.width + int(point1.position.x + s*stepX)] > 50){
                ROS_WARN_STREAM("Path is not feasible. Error on point: " +
                                std::to_string(point1.position.y) + " " +
                                std::to_string(point1.position.x));
//                ROS_WARN_STREAM("3");
                transformPath();
                return false;}
        }

    }

    for(auto point : path.poses){

        if(grid.data[point.position.y*grid.info.width + point.position.x] > 50) {ROS_WARN_STREAM("Path is not feasible"); transformPath(); return false;}
//        if(map.CellIsObstacle(point.position.y, point.position.x)) {transformPath(); return false;}
    }

    transformPath();
    return true;
}

void Planner::fillPath(std::list<Node> nodePath){
    path.poses.clear();
    path.header.frame_id = grid.header.frame_id;
    geometry_msgs::Pose point;

    for (auto node : nodePath){
        point.position.x = node.j;
        point.position.y = node.i;
        path.poses.push_back(point);
    }
    path.poses[path.poses.size() - 1].orientation = goal.pose.orientation;
}

void Planner::fillPathVis(){
    vis_path.points.clear();
    vis_path.header.frame_id = path.header.frame_id;
    for (auto pose : path.poses){
        geometry_msgs::Point point;
        point = pose.position;
        vis_path.points.push_back(point);
    }
}

void Planner::transformPath(){
    geometry_msgs::Pose point;
    for(int i=0; i<path.poses.size(); ++i) {
        point = path.poses[i];
        point = rescaleFromGrid(point);
        point = transformPoseToTargetFrame(point, path.header.frame_id, globalFrame);


        path.poses[i] = point;
    }

    path.header.frame_id = globalFrame;
}

void Planner::transformPathBack(){
    geometry_msgs::Pose point;
    for(int i=0; i<path.poses.size(); ++i) {
        point = path.poses[i];
        point = transformPoseToTargetFrame(point, path.header.frame_id, grid.header.frame_id);

        point = rescaleToGrid(point);

        path.poses[i] = point;
    }

    path.header.frame_id = globalFrame;
}

void Planner::publish(){
    path.header.stamp = ros::Time::now();
    path.header.frame_id = globalFrame;

    if(path.poses.size() != 0) path.poses.erase(path.poses.begin());
    vis_path.header.frame_id = globalFrame;
    trajPub.publish(path);
    visTrajPub.publish(vis_path);
}

geometry_msgs::Pose Planner::transformPoseToTargetFrame(geometry_msgs::Pose poseIn, std::string poseFrame, std::string targetFrame){
    geometry_msgs::TransformStamped transform;
    try {
        transform = tfBuffer.lookupTransform(poseFrame, targetFrame, ros::Time(0));
    }catch (tf2::TransformException &ex) {
        ROS_WARN("%s", ex.what());
        return poseIn;
    }

    geometry_msgs::Pose poseOut;
    poseOut = poseIn;
    poseOut.position.x -= transform.transform.translation.x;
    poseOut.position.y -= transform.transform.translation.y;
    auto angle = yaw(transform.transform.rotation);
    auto new_x = poseOut.position.x*cos(angle) + poseOut.position.y*sin(angle);
    auto new_y = -poseOut.position.x*sin(angle) + poseOut.position.y*cos(angle);
    poseOut.position.x = new_x;
    poseOut.position.y = new_y;


    auto angle_orig = yaw(poseIn.orientation);
    angle_orig -= angle;
    poseOut.orientation.x = 0;
    poseOut.orientation.y = 0;
    poseOut.orientation.z = sin(angle_orig/2);
    poseOut.orientation.w = cos(angle_orig/2);
//
//    tf2::Quaternion q_orig, q_rot, q_new;
//    tf2::convert(poseOut.orientation , q_orig);
//    tf2::convert(transform.transform.rotation, q_rot);
//    q_new = q_rot*q_orig;
//    q_new.normalize();
//    tf2::convert(q_new, poseOut.orientation);

    return poseOut;
}

geometry_msgs::Pose Planner::rescaleToGrid(geometry_msgs::Pose Pose){

    Pose.position.x -= grid.info.origin.position.x;
    Pose.position.y -= grid.info.origin.position.y;
    Pose.position.x = int(Pose.position.x / grid.info.resolution);
    Pose.position.y = int(Pose.position.y / grid.info.resolution);
    return Pose;
}

geometry_msgs::Pose Planner::rescaleFromGrid(geometry_msgs::Pose Pose){
    Pose.position.x = (Pose.position.x) * grid.info.resolution;
    Pose.position.y = (Pose.position.y) * grid.info.resolution;
    Pose.position.x += grid.info.origin.position.x;
    Pose.position.y += grid.info.origin.position.y;
    return Pose;
}

bool Planner::freeRobot(){

    auto startI = map.start_i;
    auto startJ = map.start_j;
    int maxSearchDist = 20;
    std::pair<int,int> closestDir (0,0);
    for(int di = -1; di < 2; ++di){
        for(int dj = -1; dj < 2; ++dj){
            startI = map.start_i;
            startJ = map.start_j;
            for(int d = 0; d < maxSearchDist; ++d){
                if(!map.CellIsObstacle(map.start_i + d*di, map.start_j + d*dj)){
                    if (d < maxSearchDist){
                        maxSearchDist = d;
                        closestDir.first = di;
                        closestDir.second = dj;
                    }
                }
                if(map.CellIsWall(map.start_i + d*di, map.start_j + d*dj)){
                   break;
                }
            }
        }
    }
    if (closestDir.first == 0 && closestDir.second == 0) return false;
    ROS_INFO_STREAM("Prev start position is: " << map.start_i << "  " << map.start_j);
    map.start_i = map.start_i + closestDir.first * maxSearchDist;
    map.start_j = map.start_j + closestDir.second * maxSearchDist;
    return true;
}

int main(int argc, char **argv){
    ros::init(argc, argv, "planner");
    tf2_ros::Buffer tfBuffer(ros::Duration(5)); //todo: remake with pointers
    tf2_ros::TransformListener tfListener(tfBuffer);


    Planner AStar_Planner(tfBuffer);
    ros::Rate r(5);

    while(ros::ok()){
        ros::spinOnce();
        r.sleep();
    }
}
