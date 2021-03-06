// state definition
#define INIT 0
#define PATH_PLANNING 1
#define RUNNING 2
#define FINISH -1

#include <cmath>
#include <pwd.h>
#include <unistd.h>

#include "geometry_msgs/PoseWithCovarianceStamped.h"
//#include <ackermann_msgs/AckermannDriveStamped.h>
#include "race/drive_param.h"
#include <project4/pid.h>
#include <project4/rrtTree.h>
#include <ros/ros.h>
#include <std_msgs/String.h>
#include <std_msgs/Time.h>
#include <tf/transform_datatypes.h>

// map spec
cv::Mat map;
double res;
int map_y_range;
int map_x_range;
double map_origin_x;
double map_origin_y;
double world_x_min;
double world_x_max;
double world_y_min;
double world_y_max;

// parameters you should adjust : K, margin, MaxStep
int margin = 5;
int K = 2000;
double MaxStep = 0.5;
int waypoint_margin = 11;
double waypoint_scale = 5.00;
double center_scale = 4.5;

// Vectoring
std::vector<point> waypoints;
std::vector<traj> path_RRT;

// Robot Parameters
point robot_pose;
// ackermann_msgs::AckermannDriveStamped cmd;
race::drive_param cmd;
double speed = 0.0;
double angle = 0.0;
double max_speed = 20.00;
double max_turn = 100;
// 60.0 * M_PI / 180.0;

// FSM state
int state;

// function definition
void setcmdvel(double vel, double deg);
void callback_state(geometry_msgs::PoseWithCovarianceStampedConstPtr msgs);
void set_waypoints();
void generate_path_RRT();
void set_drive_param(ros::Publisher cmd_vel_pub, PID *pid_ctrl,
                     int look_ahead_idx);

int main(int argc, char **argv) {
    ros::init(argc, argv, "slam_main");
    ros::NodeHandle n;

    ros::Publisher cmd_vel_pub =
        n.advertise<race::drive_param>("/drive_parameters", 100); // TODO

    ros::Subscriber robot_pose_sub =
        n.subscribe("/amcl_pose", 100, callback_state);
    // Initialize topics
    /*ros::Publisher cmd_vel_pub =
        n.advertise<ackermann_msgs::AckermannDriveStamped>(
            "/vesc/high_level/ackermann_cmd_mux/input/nav_0", 1);

    ros::Subscriber gazebo_pose_sub =
        n.subscribe("/amcl_pose", 100, callback_state);*/
    std::cout << "Initialized Topics" << std::endl;

    // FSM
    state = INIT;
    bool running = true;
    PID *pid_ctrl = new PID(1.0, 0.07, 0.08);
    ros::Rate control_rate(60);
    int look_ahead_idx = 0;

    while (running) {
        switch (state) {
        case INIT: {
            // Load Map
            char *user = getpwuid(getuid())->pw_name;
            cv::Mat map_org = cv::imread(
                                  (std::string("/home/scarab3") +
                                   std::string("/catkin_ws/src/hwsetup/src/hwsetup3_rot.pgm"))
                                  .c_str(),
                                  CV_LOAD_IMAGE_GRAYSCALE);

            cv::transpose(map_org, map);
            cv::flip(map, map, 1);

            map_y_range = map.cols;
            map_x_range = map.rows;
            map_origin_x = map_x_range / 2.0 - 0.5;
            map_origin_y = map_y_range / 2.0 - 0.5;
            world_x_min = -2.5;
            world_x_max = 2.5;
            world_y_min = -2.425;
            world_y_max = 2.425;
            res = 0.05;
            std::cout << "Loaded Map" << std::endl;

            if (!map.data) {
                // Check for invalid input
                std::cout << "Could not open or find the image" << std::endl;
                return -1;
            }
            state = PATH_PLANNING;
        }
        break;
        case PATH_PLANNING: {
            // Set Way Points
            std::cout << "Setting Waypoints" << std::endl;
            set_waypoints();

            // RRT
            std::cout << "Generating new path" << std::endl;
            do {
                generate_path_RRT();
                // MaxStep = (MaxStep < 0.5) ? 2.5 : MaxStep - 0.1;
            } while (path_RRT.size() == 0);

            for (auto i : path_RRT) {
                i.print();
            }
            std::cout << "Initializing Robot" << std::endl;
            ros::spinOnce();
            ros::Rate(0.33).sleep();
            state = RUNNING;
        }
        break;
        case RUNNING: {
            if (path_RRT.size() == 0) {
                printf("Path is empty.\n");
                state = FINISH;
                continue;
            }

            double dist_to_target = robot_pose.distance(path_RRT[look_ahead_idx].x,
                                    path_RRT[look_ahead_idx].y);
            if (dist_to_target <= 0.3) {
                std::cout << "New destination" << std::endl;
                printf("x, y : %.2f, %.2f \n", path_RRT[look_ahead_idx].x,
                       path_RRT[look_ahead_idx].y);
                look_ahead_idx++;
                if (look_ahead_idx == path_RRT.size()) {
                    std::cout << "Circuit Complete" << std::endl;
                    state = FINISH;
                    continue;
                }
            }

            set_drive_param(cmd_vel_pub, pid_ctrl, look_ahead_idx);

            /* printf("Debug Parameters\n"); */
            /* printf("Speed, Angle : %.2f, %.2f \n", speed, angle); */
            /* printf("Car Pose : %.2f,%.2f,%.2f,%.2f,%.2f \n", robot_pose.x, */
            /*        robot_pose.y, robot_pose.th, angle,
             * path_RRT[look_ahead_idx].th); */

            ros::spinOnce();
            control_rate.sleep();
        }
        break;
        case FINISH: {
            cmd.velocity = 0.0;
            cmd.angle = 0.0;
            cmd_vel_pub.publish(cmd);
            running = false;
            ros::spinOnce();
            control_rate.sleep();
        }
        break;
        default:
        { } break;
        }
    }
    return 0;
}

void set_drive_param(ros::Publisher cmd_vel_pub, PID *pid_ctrl,
                     int look_ahead_idx) {
    // speed = max_speed;
    speed = max_speed -
            2.0 / (1.0 + (robot_pose.distance(path_RRT[look_ahead_idx].x,
                          path_RRT[look_ahead_idx].y)));
    angle =
        -400 * pid_ctrl->get_control(robot_pose, path_RRT[look_ahead_idx]) / M_PI;

    // Validate Speed
    // speed = (speed > max_speed) ? max_speed : speed;
    // speed = (speed < -max_speed) ? -max_speed : speed;
    // Validate Angle
    angle = (angle > max_turn) ? max_turn : angle;
    angle = (angle < -max_turn) ? -max_turn : angle;

    cmd.velocity = speed;
    cmd.angle = angle;
    cmd_vel_pub.publish(cmd);
}

void set_waypoints() {
    waypoints.clear();
    point waypoint_candid[4];

    waypoint_candid[0].x = -0.75;          //-2.1
    waypoint_candid[0].y = 1.2;            //-1.1
    waypoint_candid[0].th = -3.141592 / 2; // 1.0715
    waypoint_candid[1].x = 1;              // 0.25;
    waypoint_candid[1].y = 1;              // 2

    int order[] = {0, 1};
    int order_size = 2;

    for (int i = 0; i < order_size; i++) {
        waypoints.push_back(waypoint_candid[order[i]]);
    }
}

/*
void set_waypoints() {
    std::srand(std::time(NULL));
    waypoints.push_back(point{-3.5, 8.5, 0.0});

    cv::Mat map_margin = addMargin(map, waypoint_margin);
*/
/*
quadrants
    ^y
1   |   0
____|____>x
    |
2   |   3
*/
// col == i == x
// row == j == y
/*
    double quadrants[4][2] = {{world_x_max, world_y_max},
        {world_x_min, world_y_max},
        {world_x_min, world_y_min},
        {world_x_max, world_y_min}
    };

    std::array<int, 3> quadrantSeq;

    // check in which quadrant starting point is => generate sequence
    if (waypoints[0].y >= 0 && waypoints[0].x >= 0) {
        quadrantSeq = {3, 2, 1};
        printf("Quadrant: %d \n", 0);
    } else if (waypoints[0].y >= 0 && waypoints[0].x <= 0) {
        quadrantSeq = {0, 3, 2};
        printf("Quadrant: %d \n", 1);
    } else if (waypoints[0].y <= 0 && waypoints[0].x <= 0) {
        quadrantSeq = {1, 0, 3};
        printf("Quadrant: %d \n", 2);
    } else if (waypoints[0].y <= 0 && waypoints[0].x >= 0) {
        quadrantSeq = {2, 1, 0};
        printf("Quadrant: %d \n", 3);
    } else {
        printf("Quadrant exception, no waypoint creation possible!");
    }

    // find one Waypoint in each quadrant
    bool foundPointQ;
    bool foundPointC;
    double x_rand, y_rand;
    int i_rand, j_rand;

    for (int i = 0; i < quadrantSeq.size(); i++) {
        foundPointQ = false;
        foundPointC = false;
        while (foundPointQ == false) {
            x_rand =
                quadrants[quadrantSeq[i]][0] -
                (double)(rand() * (quadrants[quadrantSeq[i]][0] /
waypoint_scale) / RAND_MAX); y_rand = quadrants[quadrantSeq[i]][1] -
                (double)(rand() * (quadrants[quadrantSeq[i]][1] /
waypoint_scale) / RAND_MAX); i_rand = round(x_rand / res + map_origin_x); j_rand
= round(y_rand / res + map_origin_y);
            // printf("Random (x,y): %.2f, %.2f \n", x_rand, y_rand);

            if ((map_margin.at<uchar>(i_rand, j_rand)) < 200) {
                // std::cout << "Drop the point."
                //          << "Wob wob wob" << std::endl;
                continue;
            } else {
                foundPointQ = true;
                printf("Waypoint Quarter found (x,y): %.2f, %.2f \n", x_rand,
y_rand); waypoints.push_back(point{x_rand, y_rand, 0.0});
            }
        }
        while (foundPointC == false) {
            if (quadrantSeq[i] == 0 || quadrantSeq[i] == 2) {
                // printf("Sektor: %d, %d \n", 0, 2);
                x_rand = quadrants[quadrantSeq[i]][0] -
                         ((double)rand() / RAND_MAX) *
                         (1.0 / center_scale * quadrants[quadrantSeq[i]][0]);
                y_rand = (1.0 / center_scale) * quadrants[quadrantSeq[i]][1] -
                         ((double)rand() / RAND_MAX) *
                         (2.0 / center_scale * quadrants[quadrantSeq[i]][1]);
            } else {
                // printf("Sektor: %d, %d \n", 1, 3);
                x_rand = (1.0 / center_scale) * quadrants[quadrantSeq[i]][0] -
                         ((double)rand() / RAND_MAX) *
                         (2.0 / center_scale * quadrants[quadrantSeq[i]][0]);
                y_rand = quadrants[quadrantSeq[i]][1] -
                         ((double)rand() / RAND_MAX) *
                         (1.0 / center_scale * quadrants[quadrantSeq[i]][1]);
            }

            i_rand = round(x_rand / res + map_origin_x);
            j_rand = round(y_rand / res + map_origin_y);
            // printf("Random (x,y): %.2f, %.2f \n", x_rand, y_rand);

            if ((map_margin.at<uchar>(i_rand, j_rand)) < 200) {
                continue;
            } else {
                foundPointC = true;
                printf("Waypoint Center found (x,y): %.2f, %.2f \n", x_rand,
y_rand); waypoints.push_back(point{x_rand, y_rand, 0.0});
            }
        }
    }

    waypoints.push_back(point{-3.5, 8.5, 0.0});

    // Waypoints for arbitrary goal points.
    // TA will change this part before scoring.
    waypoints.push_back(point{1.5, 1.5, 0.0});
    waypoints.push_back(point{-2.0, -9.0, 0.0});
}*/

void callback_state(geometry_msgs::PoseWithCovarianceStampedConstPtr msgs) {
    robot_pose.x = msgs->pose.pose.position.x;
    robot_pose.y = msgs->pose.pose.position.y;
    robot_pose.th = tf::getYaw(msgs->pose.pose.orientation);
    printf("x,y : %f,%f \n", robot_pose.x, robot_pose.y);
}

void generate_path_RRT() {
    if (waypoints.size() < 2) {
        std::cout << "Paths require more than 1 point" << std::endl;
        exit(3);
    }
    rrtTree tree =
        rrtTree(waypoints, map, map_origin_x, map_origin_y, res, margin);
    path_RRT = tree.generateRRT(world_x_max, world_x_min, world_y_max,
                                world_y_min, K, MaxStep);

    if (path_RRT.size() != 0) {
        printf("New rrtTree generated. Size of Tree: %d\n", tree.size());
        printf("New trajectory generated. Size of Path %zu\n", path_RRT.size());
        tree.visualizeTree(path_RRT);
    }
}
