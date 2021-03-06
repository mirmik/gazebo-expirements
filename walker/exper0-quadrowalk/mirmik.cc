#include <mirmik.h>

#include <igris/util/iteration_counter.h>

namespace gazebo
{
	extern physics::WorldPtr WORLD;

	double evaltime()
	{
		auto t = WORLD->SimTime();
		return (double)t.sec + (double)t.nsec / 1000000000.;
	}


	class ModelPush : public ModelPlugin
	{
	private:
		physics::ModelPtr model;
		event::ConnectionPtr updateConnection;

		int inited = 0;

		double lasttime;
		double starttime;
		double delta;

		BodyController body_controller;

	public:
		double evaltime()
		{
			auto t = WORLD->SimTime();
			return (double)t.sec + (double)t.nsec / 1000000000.;
		}

		void Reset()
		{
			lasttime = evaltime();
			starttime = evaltime();

			inited = 0;
		}

		void Load(physics::ModelPtr _parent, sdf::ElementPtr _sdf)
		{
			(void) _sdf;
			this->model = _parent;
			this->updateConnection = event::Events::ConnectWorldUpdateBegin(
			                             std::bind(&ModelPush::OnUpdate, this));

			Reset();
		}

	public:

		linalg::vec<double, 2> position_integral {};
		void OnUpdate()
		{
			double curtime = evaltime();
			delta = curtime - lasttime;
			double time = curtime - starttime;


			if (!inited)
			{
				body_controller.init(model);

				nos::reset_terminal();
				inited = 1;
				lasttime = curtime;

				for (int i = 0; i < 6; ++i)
				{
					body_controller.legs[i].regulators[1].spd_A = 5.33;
					body_controller.legs[i].regulators[1].update_regs();

					body_controller.legs[i].regulators[0].spd_A = 5.33;
					body_controller.legs[i].regulators[0].update_regs();

					// Настройки для нулевого

					//body_controller.legs[i].enable_provide_feedback();
				}

				return;
			}

			if (curtime < 5)
			{
				for (auto * a : body_controller.forward_legs) { a->regulators[0].position_target = M_PI / 8; }
				for (auto * a : body_controller.middle_legs) { a->regulators[0].position_target = 0; }
				for (auto * a : body_controller.backward_legs) { a->regulators[0].position_target = -M_PI / 8; }

				for (int i = 0; i < 6; ++i)
				{
					//body_controller.legs[i].regulators[0].speed2_loop_enabled = true;
					//body_controller.legs[i].regulators[0].position_loop_enabled = true;
					//body_controller.legs[i].regulators[0].position_target = 0;
					body_controller.legs[i].regulators[0].speed2_target = 0;
					body_controller.legs[i].regulators[0].Control(delta, Speed2Mode);

					body_controller.legs[i].regulators[1].speed2_target = 0;
					body_controller.legs[i].regulators[1].position_target = -M_PI / 8;
					body_controller.legs[i].regulators[1].Control(delta, Speed2Mode);

					body_controller.legs[i].regulators[2].speed2_target = 0;
					body_controller.legs[i].regulators[2].position_target = M_PI / 2;
					body_controller.legs[i].regulators[2].Control(delta, Speed2Mode);

					body_controller.legs[i].reset_position_target();
				}
			}
			else
			{
				for (int i = 0; i < 6; ++i)
				{
					//body_controller.legs[i].position_loop_enabled = false;
					//body_controller.legs[i].speed2_loop_enabled = false;
					//body_controller.legs[i].speed_target = {0.4*sin(evaltime()*1), 0.4*cos(evaltime()*1), 0};
					//body_controller.legs[i].speed_target = {0,0,0};
					//body_controller.legs[i].serve(delta, SpeedMode, SpeedMode);
				}
				body_controller.serve(delta);
			}


			//nos::println();
			/*for (int i = 0; i < 6; ++i)
			{
				nos::println(
				    body_controller.legs[i].relative_output_pose2().lin);
			}*/
			//nos::println(rabbit::gazebo_link_pose(model->GetLink("body")));

			lasttime = curtime;
		}

	};

	// Register this plugin with the simulator
	GZ_REGISTER_MODEL_PLUGIN(ModelPush)
}



gazebo::LegController::LegController(
    physics::ModelPtr model,
    BodyController * body_controller,
    int number)
{
	this->body_controller = body_controller;
	this->relax_pose = relax_pose;

	body_link = model->GetLink("body");

	shoulder_link = model->GetLink(nos::format("shoulder_{}", number));
	high_link = model->GetLink(nos::format("high_leg_{}", number));
	low_link = model->GetLink(nos::format("low_leg_{}", number));
	fin_link = model->GetLink(nos::format("fin_leg_{}", number));

	shoulder_joint = model->GetJoint(nos::format("joint_shoulder_{}", number));
	high_joint = model->GetJoint(nos::format("joint_high_{}", number));
	low_joint = model->GetJoint(nos::format("joint_low_{}", number));
	fin_joint = model->GetJoint(nos::format("joint_fin_{}", number));

	final_link_local_pose =
	    rabbit::gazebo_link_pose(low_link).inverse() *
	    rabbit::gazebo_link_pose(fin_link);

	final_link_local_pose.lin.x *= 2;

	joints.push_back(shoulder_joint);
	joints.push_back(high_joint);
	joints.push_back(low_joint);

	auto joint_anchor_base = rabbit::gazebo_joint_anchor_pose(joints[0]);
	for (auto j : joints)
	{
		joint_anchors.push_back(
		    joint_anchor_base.inverse() * rabbit::gazebo_joint_anchor_pose(j));
	}

	for (auto j : joints)
		joint_local_axes.push_back(
		    rabbit::gazebo_joint_local_axis(j));

	for (auto j : joints)
		regulators.emplace_back(j);

	joint_transes.resize(joints.size());
	joint_coordinates.resize(joints.size());
	joint_poses.resize(joints.size());
	sensivities.resize(joints.size());
	signals.resize(joints.size());

	inited_shoulder_pose = relative_shoulder_pose();

	A_matrix_data.resize(sensivities.size() * 3);
}

void gazebo::LegController::SpeedControl(linalg::vec<double, 3> vec)
{
	//mode = LegMode::SpeedMode;
	speed_target = vec;
}

void gazebo::LegController::PositionControl(linalg::vec<double, 3> vec)
{
	//mode = LegMode::PositionMode;
	position_target = vec;
}

bool gazebo::LegController::is_landed()
{
	//auto fin_reaction = rabbit::gazebo_joint_reaction(fin_joint);
	//bool ret = fin_reaction.lin.z > 1e-5;
	//PRINT(fin_reaction.lin.z);

	auto pose = rabbit::gazebo_link_pose(fin_link);

	return pose.lin.z < 0.305;
}

void gazebo::LegController::serve(double delta, uint8_t legmode, uint8_t regulmode)
{
	auto body_pose = rabbit::gazebo_link_pose(
	                     body_controller->model->GetLink("body"));
	auto react = body_pose.inverse().rotate_screw(reaction());
	react2 = react2 + (react - react2) * 1 * delta;
	//PRINT(react2);

	for (size_t i = 0; i < joints.size(); ++i)
	{
		joint_coordinates[i] = joints[i]->Position();
	}

	for (size_t i = 0; i < joints.size(); ++i)
	{
		joint_transes[i] = rabbit::htrans3<double>(
		                       joint_local_axes[i]
		                       * joint_coordinates[i]);
	}

	rabbit::htrans3 <double> accumulator = {};

	//joint_poses[0] = accumulator;

	for (int i = 0; i < joints.size(); ++i)
	{
		accumulator *= joint_anchors[i] * joint_transes[i];
		joint_poses[i] = accumulator;
	}

	accumulator *= final_link_local_pose;//htrans3<double>{{0,0,0,1}, {-1,0,0}};

	auto fin_local_pose = accumulator;

	for (int i = 0; i < joints.size(); ++i)
	{
		auto body_frame_axis = joint_poses[i]
		                       .rotate_screw(joint_local_axes[i]);

		auto arm = fin_local_pose.lin - joint_poses[i].lin;

		sensivities[i] = body_frame_axis.kinematic_carry(arm);
	}

	ralgo::matrix_view<double> matrix_A(A_matrix_data.data(),
	                                    3, sensivities.size());

	for (int i = 0; i < sensivities.size(); ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			matrix_A[j][i] = sensivities[i].lin[j];
		}
	}

	if (legmode & Speed2Loop)
	{
		nos::println("Speed2 loop");
		position_target += speed2_target * delta;
	}

	if (legmode & PositionLoop)
	{
		nos::println("Position loop");
		auto curpos = relative_output_pose2().lin;
		position_error = position_target - curpos;

		position_integral += position_error * delta;

		auto Ki = 0;
		auto Kp = 1;

		speed_target =
		    position_error * Kp
		    + position_integral * Ki;
		- linalg::vec<double, 3> {0, 0, react2.lin.z} * 0.2;
	}

	//PRINT(react);
	ralgo::svd_backpack(signals, speed_target, matrix_A);

	for (int i = 0; i < joints.size(); ++i)
	{
		if (regulmode == Speed2Mode)
			regulators[i].speed2_target = signals[i];

		if (regulmode == SpeedMode)
			regulators[i].speed_target = signals[i];

		regulators[i].Control(delta, regulmode);
	}
}

gazebo::Regulator::Regulator(physics::JointPtr joint)
	: joint(joint)
{
	update_regs();
}

void gazebo::Regulator::reset()
{
	speed_error = 0;
	position_error = 0;

	speed_target = 0;
	position_target = 0;

	speed_integral = 0;
	position_integral = 0;

	speed2_target = 0;
}

void gazebo::Regulator::Control(double delta, uint8_t mode)
{
	double current_position = joint->Position(0);
	double current_speed = joint->GetVelocity(0);

	if (mode & Speed2Loop)
	{
		position_target += speed2_target * delta;
	}

	if (mode & PositionLoop)
	{
		position_error = position_target - current_position;
		position_integral += position_error * delta;

		if (position_integral > position_integral_limit ||
		        position_integral < -position_integral_limit
		   )
		{
			nos::println("Position integral is clamped");
		}

		position_integral = igris::clamp(position_integral,
		                                 -position_integral_limit,
		                                 position_integral_limit);

		speed_target =
		    pos_kp * position_error +
		    pos_ki * position_integral
		    - control_signal * ForceKoeff;
	}

	//do_after_iteration(3) exit(0);


	speed_error = speed_target - current_speed;
	speed_integral += speed_error * delta;

	if (speed_integral_limit)
	{

		if (speed_integral > speed_integral_limit ||
		        speed_integral < -speed_integral_limit
		   )
		{
			nos::println("Speed integral is clamped");
		}

		speed_integral = igris::clamp(speed_integral,
		                              -speed_integral_limit,
		                              speed_integral_limit);
	}
	control_signal =
	    spd_kp * speed_error +
	    spd_ki * speed_integral;

	joint->SetForce(0, control_signal);
}


gazebo::BodyController::BodyController() :
	trot_controller(this)
{}

void gazebo::BodyController::init(physics::ModelPtr model)
{
	this->model = model;
	legs.clear();

	for (int i = 0; i < 6; ++i)
		legs.emplace_back(model, this, i);

	for (int i = 0; i < 6; ++i)
		legs_ptrs.emplace_back(&legs[i]);

	for (auto l : legs) l.enable_provide_feedback();

	legs[0].relax_pose = { -2.2, 0.8, 0 };
	legs[1].relax_pose = { -2.2, 0, 0 };
	legs[2].relax_pose = { -2.2, -0.8, 0 };
	legs[3].relax_pose = { 2.2, 0.8, 0 };
	legs[4].relax_pose = { 2.2, 0, 0 };
	legs[5].relax_pose = { 2.2, -0.8, 0 };

	forward_legs.push_back(&legs[0]);
	forward_legs.push_back(&legs[3]);

	middle_legs.push_back(&legs[1]);
	middle_legs.push_back(&legs[4]);

	backward_legs.push_back(&legs[2]);
	backward_legs.push_back(&legs[5]);

	body_target = {{0, 0, 0, 1}, {0, 0, 1.5}};

	trot_controller.init();
}

void gazebo::BodyController::serve(double delta)
{
	if (inited==false) 
	{
		inited=true;
		double start_body_time = evaltime();
	}

	body_speed_control = {0, 0, 0};
	body_pose = rabbit::gazebo_link_pose(model->GetLink("body"));

	double time = evaltime() - start_body_time;
	
	double T = 20;
	double R = 10;
	if (time > 5) { 
		time = time - 5;
		double t2 = time - (int)(time / T / 4)*T*4;

		double alpha = time/128;
		PRINT(t2);
		//body_target = {{0, 0, sin(alpha), cos(alpha)}, {0, 0.35*time, 1.5}};

		if (t2 < T) 
		{
			double koeff = (t2) / T;
			body_target = {{0, 0, 0, 1}, {0, R*koeff, 1.5}};			 
		} 
		else if (t2 < 2*T) 
		{
			double koeff = (t2-T) / T;
			body_target = {{0, 0, 0, 1}, {R*koeff, R, 1.5}};	
		}
		else if (t2 < 3*T) 
		{
			double koeff = (t2-2*T) / T;
			body_target = {{0, 0, 0, 1}, {R, R-R*koeff, 1.5}};	
		}
		else if (t2 < 4*T) 
		{
			double koeff = (t2-3*T) / T;
			body_target = {{0, 0, 0, 1}, {R-R*koeff, 0, 1.5}};
		}
	}

	// Режим удержания позиции.
	//body_target.lin += body_speed_control * delta;

	std::vector<LegController*> grp
	{
		&legs[0],
		&legs[2],
		&legs[4],
	};


	trot_controller.serve(delta);
	//body_serve_with_group(legs_ptrs, delta);
}

void gazebo::BodyController::body_serve_with_group(
    const std::vector<LegController *>& legs,
    double delta)
{
	body_error = (body_pose.inverse() * body_target).to_screw();
	//PRINT(body_target);
	//PRINT(body_error);
	auto body_speed = rabbit::gazebo_link_speed(model->GetLink("body"));

	body_error_integral += body_error * delta;

	body_speed_target =
	    body_error * pos_kp
	    + body_error_integral * pos_ki * 0
	    - body_speed * 0;

	for (auto l : legs)
	{

		auto lin = l->relative_output_pose2().lin;
		lin.z = 0;
		if (length(lin) > 3) 
		{
			nos::println("INterrupt"); 
			return; 
		}


		auto shoulder_pose = l->inited_shoulder_pose;
		auto arm = shoulder_pose.lin;
		auto signal =
		    body_speed_target.kinematic_carry(arm);

		l->speed_target = -signal.lin;
		l->serve(delta, SpeedMode, SpeedMode);
	}
};

rabbit::htrans3<double> gazebo::LegController::relative_shoulder_pose()
{
	auto body_pose = rabbit::gazebo_link_pose(body_link);
	auto shoulder_pose = rabbit::gazebo_link_pose(shoulder_link);
	return body_pose.inverse() * shoulder_pose;
}

rabbit::htrans3<double> gazebo::LegController::relative_output_pose2()
{
	auto body_pose = rabbit::gazebo_link_pose(body_link);
	auto fin_pose = rabbit::gazebo_link_pose(fin_link);
	auto root_pose = body_pose * inited_shoulder_pose;

	return root_pose.inverse() * fin_pose;
}

void gazebo::BodyController::relax_movement_with_group(
    const std::vector<LegController *>& legs,
    double delta,
    double level,
    linalg::vec<double, 3> speed)
{
	for (auto l : legs)
	{
		auto output_pose = l->relative_output_pose2();
		auto xy = linalg::vec<double,3>{output_pose.lin.x, output_pose.lin.y, 0};
		auto z = output_pose.lin.z;

		auto xydir = l->relax_pose - xy;

		auto signal =
		    linalg::vec<double, 3> { 0, 0, -1.5 + 0.4 - z }
		    + body_speed_target.lin * 1.5
		    + xydir;

		l->speed_target = signal;
		l->serve(delta, SpeedMode, SpeedMode);
	}
};

void gazebo::BodyController::TrotControl(double delta)
{
	trot_controller.serve(delta);
}

