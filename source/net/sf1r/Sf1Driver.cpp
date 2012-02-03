/* 
 * File:   Sf1Driver.cpp
 * Author: paolo
 * 
 * Created on January 10, 2012, 10:07 AM
 */

#include "net/sf1r/Sf1Driver.hpp"
#include "ConnectionPool.hpp"
#include "JsonWriter.hpp"
#include "RawClient.hpp"
#include <boost/lexical_cast.hpp>
#include <glog/logging.h>


NS_IZENELIB_SF1R_BEGIN


using ba::ip::tcp;
using boost::system::system_error;
using std::string;


/**
 * Max sequence number.
 * Valued 4294967294 as defined in the Ruby client:
 * \code 
 * MAX_SEQUENCE = (1 << 31) - 2
 * \endcode
 * It is also the upper limit of the number of requests in a batch request.
 * @see \ref limits
 */
const uint32_t MAX_SEQUENCE = std::numeric_limits<uint32_t>::max() - 1;


Sf1Driver::Sf1Driver(const string& host, const uint32_t& port, 
        const Sf1Config& parameters, 
        const Format& format) throw(ServerError) 
        : sequence(1), resolver(service), 
          query(/*tcp::v4(),*/ host, boost::lexical_cast<string>(port)) { // TODO: restrict to v4?
    try {
        setFormat(format);
        
        iterator = resolver.resolve(query);
        initPool(parameters);
        
        LOG(INFO) << "Driver ready";
    } catch (system_error& e) {
        string message = e.what();
        LOG(ERROR) << message;
        throw ServerError(message);
    }
}


Sf1Driver::~Sf1Driver() {
    delete pool;
    
    LOG(INFO) << "Driver closed";
}


void
Sf1Driver::setFormat(const Format& format) {
    switch (format) {
    case JSON:
    default:
        writer.reset(new JsonWriter);
        LOG(INFO) << "Using JSON data format";
    }
}


void
Sf1Driver::initPool(const Sf1Config& params) {
    pool = new ConnectionPool(service, iterator, 
                params.initialSize, params.resize, params.maxSize);
}


/// Split a string on delimiter character into a vector.
static
std::vector<string>&
split(const string& s, const char& delim, std::vector<string>& elems) {
    std::stringstream ss(s);
    string item;
    while(getline(ss, item, delim)) {
        if (item.empty()) {
            // skip empty elements
            continue;
        }
        elems.push_back(item);
    }
    return elems;
}


string
Sf1Driver::call(const string& uri, const string& tokens, string& request) 
throw(ClientError, ServerError) {
    // parse uri for controller and action
    std::vector<string> elems;
    split(uri, '/', elems);
    
    // controller is mandatory
    if (elems.size() < 1) {
        LOG(ERROR) << "Require controller name: [" << uri << "]";
        throw ClientError("Require controller name");
    }
    
    string controller = elems.at(0);
    DLOG(INFO) << "controller: " << controller;
    
    // action is optional
    string action;
    if (elems.size() > 1) {
        action = elems.at(1);
    } 
    DLOG(INFO) << "action    : " << action;
    
    // check request
    if (not writer->checkData(request)) {
        LOG(ERROR) << "Malformed request: [" << request << "]";
        throw ClientError("Malformed request");
    }
    
    writer->setHeader(controller, action, tokens, request);
    LOG(INFO) << "sending request: " << request;
    
    // increment sequence
    if (++sequence == MAX_SEQUENCE) {
        sequence = 1; // sequence == 0 means server error
    }
    
    try {
        RawClient& client = pool->acquire(); // FIXME: this throws ConnectionPoolError
        
        client.sendRequest(sequence, request);
        Response response = client.getResponse();
        uint32_t responseSequence = response.get<RESPONSE_SEQUENCE>();
        
        if (responseSequence == 0) {
            LOG(ERROR) << "Zero sequence";
            pool->release();
            throw ServerError("Zero Sequence");
        }

        if (sequence != responseSequence) {
            LOG(ERROR) << "Unmatched sequence number: "
                       << "in = [" << sequence << "] out = [" << responseSequence << "]";
            pool->release();
            throw ServerError("Unmatched sequence number");
        }
        
        string responseBody = response.get<RESPONSE_BODY>();
        
        if (not writer->checkData(responseBody)) { // This should never happen
            LOG(ERROR) << "Malformed response: [" << responseBody << "]";
            pool->release();
            throw ServerError("Malformed response");
        }

        pool->release();
        return responseBody;
    } catch (ClientError& e) {
        // do not intercept ClientErrors
        throw e;
    } catch (ServerError& e) {
        // do not intercept ServerErrors
        throw e;
    } catch (std::exception& e) {
        string message = e.what();
        LOG(ERROR) << message;
        throw e;
    }
}


NS_IZENELIB_SF1R_END
