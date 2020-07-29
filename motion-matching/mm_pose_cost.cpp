

float pose_cost()
{
	float cost = 0.f;

	cost += Dist(current_pose.l_ankle.pos, candidate_pose.l_ankle.pos) * w1;
	cost += Dist(current_pose.l_ankle.vel, candidate_pose.l_ankle.vel) * w2;

	cost += Dist(current_pose.r_ankle.pos, candidate_pose.r_ankle.pos) * w3;
	cost += Dist(current_pose.r_ankle.vel, candidate_pose.r_ankle.vel) * w4;

	cost += Dist(current_pose.spine.pos, candidate_pose.spine.pos) * w5;
	cost += Dist(current_pose.spine.vel, candidate_pose.spine.vel) * w6;

	...

	return cost;
}



float trajectoy_cost()
{
	float cost = 0.f;

	for (int i = 0; i < num_samples; i++)
		cost += Dist(desired_trajectory.sample[i], candidate_pose_trajectory.sample[i]);

	return cost;
}