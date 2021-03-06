/** sensor.h                                                       -*- C++ -*-
    Jeremy Barnes, 4 December 2014
    Copyright (c) 2014 Datacratic Inc.  All rights reserved.

    This file is part of MLDB. Copyright 2015 Datacratic. All rights reserved.

    Interface for sensors into MLDB.
*/

#include "mldb/types/value_description_fwd.h"
#include "mldb/core/mldb_entity.h"
#include <set>

// NOTE TO MLDB DEVELOPERS: This is an API header file.  No includes
// should be added, especially value_description.h.


#pragma once

namespace MLDB {

struct RestRequest;
struct RestConnection;
struct RestRequestParsingContext;
struct MldbEngine;
struct Sensor;
struct ExpressionValue;
struct ExpressionValueInfo;

typedef EntityType<Sensor> SensorType;


/*****************************************************************************/
/* SENSOR                                                                    */
/*****************************************************************************/

struct Sensor: MldbEntity {
    Sensor(MldbEngine * engine);

    virtual ~Sensor();

    MldbEngine * engine;
    
    virtual std::string getKind() const
    {
        return "sensor";
    }

    virtual Any getStatus() const;
    
    virtual Any getVersion() const;

    /** Return the latest value (and the timestamp at which it was read)
        for the sensor.  This is non-const but must be thread safe.
    */
    virtual ExpressionValue latest() = 0;

    /** Return the ExpressionValueInfo for the result of calling
        latest().
    */
    virtual std::shared_ptr<ExpressionValueInfo>
    resultInfo() const = 0;

    /** Method to overwrite to handle a request.  By default, the sensor
        will return that it can't handle any requests.
    */
    virtual RestRequestMatchResult
    handleRequest(RestConnection & connection,
                  const RestRequest & request,
                  RestRequestParsingContext & context) const;

    /** Method to respond to a route under /v1/sensors/xxx/doc, which
        should serve up the documentation.  Default implementation
        says no documentation is available.
    */
    virtual RestRequestMatchResult
    handleDocumentationRoute(RestConnection & connection,
                  const RestRequest & request,
                  RestRequestParsingContext & context) const;

    /** Method to respond to a route under /v1/sensors/xxx/static, which
        should serve up static resources for the sensor.  Default implementation
        says no static resources are available.
    */
    virtual RestRequestMatchResult
    handleStaticRoute(RestConnection & connection,
                      const RestRequest & request,
                      RestRequestParsingContext & context) const;
};

/*****************************************************************************/
/* UTILITIES                                                                 */
/*****************************************************************************/

std::shared_ptr<Sensor>
obtainSensor(MldbEngine * engine,
             const PolyConfig & config,
             const std::function<bool (const Json::Value & progress)> & onProgress
                 = nullptr);

std::shared_ptr<Sensor>
createSensor(MldbEngine * engine,
             const PolyConfig & config,
             const std::function<bool (const Json::Value & progress)> & onProgress
                 = nullptr);

std::shared_ptr<SensorType>
registerSensorType(const Package & package,
                   const Utf8String & name,
                   const Utf8String & description,
                   std::function<Sensor * (RestDirectory *,
                                           PolyConfig,
                                           const std::function<bool (const Json::Value)> &)>
                   createEntity,
                   TypeCustomRouteHandler docRoute,
                   TypeCustomRouteHandler customRoute,
                   std::shared_ptr<const ValueDescription> config,
                   std::set<std::string> registryFlags);

/** Register a new sensor kind.  This takes care of registering everything behind
    the scenes.
*/
template<typename SensorT, typename Config>
std::shared_ptr<SensorType>
registerSensorType(const Package & package,
                        const Utf8String & name,
                        const Utf8String & description,
                        const Utf8String & docRoute,
                        TypeCustomRouteHandler customRoute = nullptr,
                        std::set<std::string> flags = {})
{
    return registerSensorType
        (package, name, description,
         [] (RestDirectory * engine,
             PolyConfig config,
             const std::function<bool (const Json::Value)> & onProgress)
         {
             auto res = new SensorT(SensorT::getOwner(engine), config, onProgress);
             res->logger = getMldbLog<SensorT>();
             return res;
         },
         makeInternalDocRedirect(package, docRoute),
         customRoute,
         getDefaultDescriptionSharedT<Config>(),
         flags);
}

template<typename SensorT, typename Config>
struct RegisterSensorType {
    RegisterSensorType(const Package & package,
                       const Utf8String & name,
                       const Utf8String & description,
                       const Utf8String & docRoute,
                       TypeCustomRouteHandler customRoute = nullptr,
                       std::set<std::string> registryFlags = {})
    {
        handle = registerSensorType<SensorT, Config>
            (package, name, description, docRoute, customRoute,
             registryFlags);
    }

    std::shared_ptr<SensorType> handle;
};

} // namespace MLDB
