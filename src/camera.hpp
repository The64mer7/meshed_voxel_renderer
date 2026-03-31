#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace WorldDirection
{
	constexpr glm::vec3 Up = glm::vec3(0.f, 1.f, 0.f);
	constexpr glm::vec3 Right = glm::vec3(1.f, 0.f, 0.f);
	constexpr glm::vec3 Forward = glm::vec3(0.f, 0.f, -1.f);
}

struct FirstPersonCameraSettings
{
	glm::vec3 position;
	glm::vec3 direction;
	float fov;
	float nearPlane;
	float farPlane;
	float width;
	float height;
	float walkSpeed;
	int keyForward, keyBackward, keyLeft, keyRight;
	glm::vec2 sensitivity;
};

class FirstPersonCamera
{
public:
	FirstPersonCamera() = default;
	FirstPersonCamera(const FirstPersonCameraSettings& settings)
	{
		Init(settings);
	}

	void Init(const FirstPersonCameraSettings& settings)
	{
		m_Position = settings.position;

		m_Pitch = asin(settings.direction.y);
		m_Yaw = atan2(settings.direction.z, settings.direction.x);

		SetProjection(settings);
	}

	void SetProjection(const FirstPersonCameraSettings& settings)
	{
		float aspect = settings.width / settings.height;
		m_ProjectionMatrix = glm::perspectiveZO(settings.fov, aspect, settings.nearPlane, settings.farPlane);
		
		float ortho_height = 20.f;
		float ortho_width = aspect * ortho_height;
		m_OrthoProjectionMatrix = glm::ortho(
			-ortho_width,
			ortho_width,
			-ortho_height,
			ortho_height,
			-settings.farPlane, settings.farPlane);
	}

	void Translate(glm::vec3 v)
	{
		m_Position += v;
	}

	void SetPosition(glm::vec3 v)
	{
		m_Position = v;
	}

	glm::vec3 GetForwardVector()
	{
		glm::vec3 forward;

		forward.x = cos(m_Yaw) * cos(m_Pitch);
		forward.y = sin(m_Pitch);
		forward.z = sin(m_Yaw) * cos(m_Pitch);

		return glm::normalize(forward);
	}

	glm::vec3 GetPosition()
	{
		return m_Position;
	}

	glm::mat4 GetViewMatrix()
	{
		return glm::lookAt(GetPosition(), GetPosition() + GetForwardVector(), WorldDirection::Up);
	}

	glm::mat4 GetProjectionMatrix()
	{
		return m_ProjectionMatrix;
	}

	glm::mat4 GetOrthoProjectionMatrix()
	{
		return m_OrthoProjectionMatrix;
	}

	void Rotate(float deltaYaw, float deltaPitch)
	{
		m_Yaw += deltaYaw;
		m_Pitch += deltaPitch;
		m_Pitch = glm::clamp(m_Pitch, glm::radians(-89.f), glm::radians(89.f));
	}

private:
	glm::vec3 m_Position;
	float m_Yaw, m_Pitch;
	glm::mat4 m_ProjectionMatrix;
	glm::mat4 m_OrthoProjectionMatrix;
};