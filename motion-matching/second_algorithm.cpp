
float best_dist = FLT_MAX;
Pose bestPose;

for (int i = 0; i < num_poses; i++)
{
	float dist_traj = trajectory_cost(desired_trajectory, pose[i].trajectory);
	float dist_pose = pose_cost(current_pose, candidate[i].pose);
	dist = dist_traj * w_trajectory + dist_pose * w_pose;
	if (dist < best_dist)
	{
		best_dist = dist;
		best_pose = pose[i];
	}
}

// play the best_pose.