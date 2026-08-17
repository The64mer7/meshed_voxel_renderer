#pragma once
#include <GLFW/glfw3.h>

class InputState
{
public:
	void update(GLFWwindow* window)
	{
		m_curr = prev();

		for (int i = 0; i <= GLFW_KEY_LAST; i++)
			set_key(i, glfwGetKey(window, i) == GLFW_PRESS);

		for (int i = 0; i <= GLFW_MOUSE_BUTTON_LAST; i++)
			set_button(i, glfwGetMouseButton(window, i) == GLFW_PRESS);

		glfwGetCursorPos(window, &mouse_x[curr()], &mouse_y[curr()]);
	}

	bool is_key_clicked(int key) const
	{
		return get_key(key) && !get_prev_key(key);
	}

	bool is_key_released(int key) const
	{
		return !get_key(key) && get_prev_key(key);
	}

	bool is_key_held(int key) const
	{
		return get_key(key);
	}

	bool get_key(int key) const
	{
		int byte_idx = key / 8;
		int bit_idx = key % 8;
		return (prev_curr_keys[curr()][byte_idx] >> bit_idx) & 1;
	}

	bool get_prev_key(int key) const
	{
		int byte_idx = key / 8;
		int bit_idx = key % 8;
		return (prev_curr_keys[prev()][byte_idx] >> bit_idx) & 1;
	}

	void set_key(int key, bool val)
	{
		int byte_idx = key / 8;
		int bit_idx = key % 8;
		prev_curr_keys[curr()][byte_idx] &= ~(1 << bit_idx);
		prev_curr_keys[curr()][byte_idx] |= (uint8_t)val << bit_idx;
	}


	bool is_button_clicked(int button) const
	{
		return get_button(button) && !get_prev_button(button);
	}

	bool is_button_released(int button) const
	{
		return !get_button(button) && get_prev_button(button);
	}

	bool is_button_held(int button) const
	{
		return get_button(button);
	}

	bool get_button(int button) const
	{
		int byte_idx = button / 8;
		int bit_idx = button % 8;
		return (prev_curr_buttons[curr()][byte_idx] >> bit_idx) & 1;
	}

	bool get_prev_button(int button) const
	{
		int byte_idx = button / 8;
		int bit_idx = button % 8;
		return (prev_curr_buttons[prev()][byte_idx] >> bit_idx) & 1;
	}

	void set_button(int button, bool val)
	{
		int byte_idx = button / 8;
		int bit_idx = button % 8;
		prev_curr_buttons[curr()][byte_idx] &= ~(1 << bit_idx);
		prev_curr_buttons[curr()][byte_idx] |= (uint8_t)val << bit_idx;
	}

	double get_mouse_x()
	{
		return mouse_x[curr()];
	}
	double get_mouse_y()
	{
		return mouse_y[curr()];
	}
	double get_mouse_dx()
	{
		return mouse_x[curr()] - mouse_x[prev()];
	}
	double get_mouse_dy()
	{
		return mouse_y[curr()] - mouse_y[prev()];
	}

private:
	uint8_t prev_curr_keys[2][(GLFW_KEY_LAST + 7) / 8] = { {0}, {0} };
	uint8_t prev_curr_buttons[2][(GLFW_MOUSE_BUTTON_LAST + 7) / 8] = { {0}, {0} };

	int m_curr = 1;
	inline int curr() const
	{
		return m_curr;
	}
	inline int prev() const
	{
		return 1 - curr();
	}

	double mouse_x[2] = { 0.0 };
	double mouse_y[2] = { 0.0 };
};