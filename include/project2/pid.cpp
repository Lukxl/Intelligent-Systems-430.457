#include <project2/pid.h>
#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846

PID::PID(double Kp_in, double Ki_in, double Kd_in)  {
    Kp = Kp_in;
    Ki = Ki_in;
    Kd = Kd_in;

    error = 0;
    error_diff = 0;
    error_sum = 0;

    freq = 0.1;
}

PID::~PID() {}

float PID::get_control(point car_pose, traj goal_pose) {

    float ctrl;
    float p;
    float i;
    float d;
    float des_angle;

    // Angle
    des_angle = atan2(goal_pose.y - car_pose.y, goal_pose.x - car_pose.x)
        - car_pose.th;
    printf("Atan, Theta: %0.2f %0.2f %0.2f\n",
            des_angle, car_pose.th, error_sum);

    // Updating Error
    error_diff = des_angle - error;
    error = des_angle;
    error_sum += error;

    // Calculating P I D
    p = Kp * error;
    i = Ki * freq * error_sum;
    d = Kd * error_diff / freq;

    // Control Value
    ctrl = p + i + d;

    return ctrl;
}
