/* 
 * File:   Sf1DistributedConfig.hpp
 * Author: Paolo D'Apice
 *
 * Created on March 26, 2012, 3:51 PM
 */

#ifndef SF1DISTRIBUTEDCONFIG_HPP
#define	SF1DISTRIBUTEDCONFIG_HPP

#include "../config.h"
#include "../Sf1Config.hpp"
#include <string>
#include <vector>

NS_IZENELIB_SF1R_BEGIN


/// Default ZooKeeper session timeout.
#define SF1_CONFIG_TIMEOUT      2000


class Sf1DistributedDriver;


/**
 * Container for the distributed driver configuration parameters:
 * - ZooKeeper session timeout
 * - routing policies
 */
struct Sf1DistributedConfig : public Sf1Config {
    
    /**
     * Default constructor.
     */
    Sf1DistributedConfig() : Sf1Config(), timeout(SF1_CONFIG_TIMEOUT) {}
    
    uint32_t timeout;             ///< ZooKeeper session timeout.
    
    /**
     * Add a broadcast routing policy.
     * @param regex A matching regular expression for the request URI.
     */
    void addBroadCast(const std::string& regex) {
        broadcast.push_back(regex);
    }
    
private:
    /// List of patterns for request broadcasting.
    std::vector<std::string> broadcast;
    
    friend class Sf1DistributedDriver;
};


NS_IZENELIB_SF1R_END

#endif	/* SF1DISTRIBUTEDCONFIG_HPP */