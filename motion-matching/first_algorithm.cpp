
float best_dist = FLT_MAX;
Pose bestPose;

for (int i = 0; i < num_poses; i++)
{
	float dist = trajectory_cost(desired_trajectory, pose[i].trajectory);
	if (dist < best_dist)
	{
		best_dist = dist;
		best_pose = pose[i];
	}
}

// play the best_pose.
