#pragma once
#include <iostream>
#include <GLFW/glfw3.h>

class ScopedTimer
{
public:
    ScopedTimer(std::string label)
    {
        m_label = label;
        m_start_time = glfwGetTime();
    }
    ~ScopedTimer()
    {
        m_end_time = glfwGetTime();
        printf("%s: %f\n", m_label.c_str(), (m_end_time - m_start_time) * 1000.f);
    }
private:
    double m_end_time;
    double m_start_time;
    std::string m_label;
};
