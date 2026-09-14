#pragma once
#include <chrono>
#include <iostream>

class ScopedTimer
{
public:
    ScopedTimer(std::string label)
    {
        m_label = label;
        m_start_time = std::chrono::high_resolution_clock::now();
    }
    ~ScopedTimer()
    {
        m_end_time = std::chrono::high_resolution_clock::now();
        printf("%s: %f\n", m_label.c_str(),
               std::chrono::duration<double>(m_end_time - m_start_time).count());
    }

private:
    std::chrono::high_resolution_clock::time_point m_start_time;
    std::chrono::high_resolution_clock::time_point m_end_time;
    std::string m_label;
};
