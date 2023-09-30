
struct
player_camera
{
  glm::vec3 Position;
  glm::vec3 Velocity;
  glm::vec3 InputAxis;

  float Pitch {0};
  float Yaw {0};

  bool Sprint = false;
  bool Locked;

  void ProcessInput()
  {
    InputAxis.x = (f32)(g_Input.GetInputDown(input::action::UP) - g_Input.GetInputDown(input::action::DOWN));
    InputAxis.y = (f32)(g_Input.GetInputDown(input::action::RIGHT) - g_Input.GetInputDown(input::action::LEFT));
    InputAxis.z = 0.0f;
  }

	void Update(float Delta)
  {
    const float cam_vel = 0.001f + (f32)Sprint * 0.01f;
	  glm::vec3 forward = { 0,0,cam_vel };
	  glm::vec3 right = { cam_vel,0,0 };
	  glm::vec3 up = { 0,cam_vel,0 };

	  glm::mat4 cam_rot = GetRotationMatrix();

	  forward = cam_rot * glm::vec4(forward, 0.f);
	  right = cam_rot * glm::vec4(right, 0.f);

	  Velocity = InputAxis.x * forward + InputAxis.y * right + InputAxis.z * up;

	  Velocity *= 10 * Delta;

	  Position += Velocity;
  }

  glm::mat4 GetViewMatrix()
  {
    glm::vec3 camPos = Position;

	  glm::mat4 cam_rot = (GetRotationMatrix());

	  glm::mat4 view = glm::translate(glm::mat4{ 1 }, camPos) * cam_rot;

	  //we need to invert the camera matrix
	  view = glm::inverse(view);

	  return view;
  }

  glm::mat4 GetProjectionMatrix(bool Reverse = true)
  {
    if (Reverse)
	  {
		  glm::mat4 pro = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 5000.0f, 0.1f);
		  pro[1][1] *= -1;
		  return pro;
	  }
	  else
    {
		  glm::mat4 pro = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 5000.0f);
		  pro[1][1] *= -1;
		  return pro;
	  }
  }

  glm::mat4 GetRotationMatrix()
  {
    glm::mat4 yaw_rot = glm::rotate(glm::mat4{ 1 }, Yaw, { 0, -1, 0 });
	  glm::mat4 pitch_rot = glm::rotate(glm::mat4{ yaw_rot }, Pitch, { -1, 0, 0 });

	  return pitch_rot;
  }
};
