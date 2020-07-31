

float best_dist = FLT_MAX;
Pose bestPose;

for (int i_cluster = 0; i < num_clusters; i_cluster++)
{
	const Cluster& cluster = clusters[i_cluster];

	if (!cluster.InBoundSphere(desired_trajectory))
		continue;	

	for (int i_pose = 0; i_pose < clusters[i_cluster].num_points; i_pose++)
	{
		float dist_traj = trajectory_cost(desired_trajectory, cluster.pose[i_pose].trajectory);
		float dist_pose = pose_cost(current_pose, cluster.pose[i_pose].pose);
		dist = dist_traj * w_trajectory + dist_pose * w_pose;
		if (dist < best_dist)
		{
			best_dist = dist;
			best_pose = pose[i];
		}
	}
}

// play the best_pose.