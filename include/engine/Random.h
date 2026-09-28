#pragma once

#include <random>

namespace engine {

class Random {
public:
    static Random& global() {
        static Random instance;
        return instance;
    }

    Random() {
    #ifdef NDEBUG
        rng = std::mt19937(std::random_device{}());
    #else
        rng = std::mt19937(0); // fixed seed
    #endif
    }

    int randomInt(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(rng);
    }

    int randomInt(int bounds) {
        std::uniform_int_distribution<int> dist(-bounds, bounds);
        return dist(rng);
    }

    float randomFloat(float min, float max) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng);
    }

    float randomFloat(float bounds) {
        std::uniform_real_distribution<float> dist(-bounds, bounds);
        return dist(rng);
    }
    
private:
    std::mt19937 rng;
};

};