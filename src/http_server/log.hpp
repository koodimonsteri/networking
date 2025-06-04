#pragma once

#include <mutex>
#include <iostream>


std::mutex logMutex;

//inline std::ostream& operator<<(std::ostream& out, const std::pair<std::string, std::string>& inPair) {
//    out << inPair.first << ": " << inPair.second;
//    return out
//}
//
//inline std::ostream& operator<<(std::ostream& out, const std::vector<std::pair<std::string, std::string>>& inVec) {
//    for (const auto& item : inVec) {
//        out << item << "\n";
//    }
//    return out;
//}


template <typename... Args>
void logf(Args&&... args) {
    std::lock_guard<std::mutex> lock(logMutex);
    (std::cout << ... << std::forward<Args>(args)) << std::endl;
}

template <typename... Args>
void logcerr(Args&&... args) {
    std::lock_guard<std::mutex> lock(logMutex);
    (std::cerr << ... << std::forward<Args>(args)) << std::endl;
}