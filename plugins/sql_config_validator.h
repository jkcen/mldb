/** sql_config_validator.h                                                       -*- C++ -*-
    Guy Dumais, 18 December 2015

    This file is part of MLDB. Copyright 2015 Datacratic. All rights reserved.

    Several templates to validate constraints on SQL statements and other parts of
    entity configs.
*/

#include "mldb/sql/sql_expression.h"
#include "types/optional.h"

#pragma once

namespace Datacratic {

namespace MLDB {


// one can chain validation of several fields it this way
// chain(validator1, chain(validator2, validator3))
template <typename ConfigType>
std::function<void (ConfigType *, JsonParsingContext &)>
chain(const std::function<void (ConfigType *, JsonParsingContext &)> & validator1,
      const std::function<void (ConfigType *, JsonParsingContext &)> & validator2) {
    return [=](ConfigType * config, JsonParsingContext & context) {
            validator1(config, context);
            validator2(config, context);
        };
}

template<typename ConfigType,
         typename FieldType, // either InputQuery or Optional<InputQuery>
         typename Constraint>
std::function<void (ConfigType *, JsonParsingContext &)>
validateQuery(FieldType ConfigType::* field, const Constraint & constraint)
{
    return [=](ConfigType * cfg, JsonParsingContext & context) {
        constraint(cfg->*field, ConfigType::name);
    };
}

template<typename ConfigType,
         typename FieldType, // either InputQuery or Optional<InputQuery>
         typename Constraint,
         typename... Constraints>
std::function<void (ConfigType *, JsonParsingContext & context)>
validateQuery(FieldType ConfigType::* field, const Constraint & constraint, Constraints&& ...constraints)
{
    // workaround of a gcc 4.8 limitation
    // parameter packs cannot be expended in lambda function
    // see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=55914
    auto validator = [=](ConfigType * cfg, JsonParsingContext & context) {
        constraint(cfg->*field, ConfigType::name);
    };

    return chain<ConfigType>(validator, validateQuery(field, constraints...));
}

/**
 *  Accept any select statement with empty GROUP BY/HAVING clause.
 *  FieldType must contain a SelectStatement named stm.
 */
struct NoGroupByHaving
{
    void operator()(const InputQuery & query, const std::string & name) const
    {
        if (query.stm) {
            if (!query.stm->groupBy.empty()) {
                throw ML::Exception(name + " does not support groupBy clause");
            }
            else if (!query.stm->having->isConstantTrue()) {
                throw ML::Exception(name + " does not support having clause");
            }
        }
    }

    void operator()(const Optional<InputQuery> & query, const std::string & name) const
    {
        if (query) operator()(*query, name);
    }
};

/**
  *  Must contain a FROM clause
 */
struct MustContainFrom
{
    void operator()(const InputQuery & query, const std::string & name) const
    {
        if (!query.stm || !query.stm->from || query.stm->from->surface.empty())
            throw ML::Exception(name + " must contain a FROM clause");
    }

    void operator()(const Optional<InputQuery> & query, const std::string & name) const
    {
        if (query) operator()(*query, name);
    }
};

/**
 *  Accept simple select expressions like column1, column2, wildcard expressions
 *  and column expressions but reject operations on columns like sum(column1, column2).
 *  FieldType must contain a SelectStatement named stm.
 */
struct PlainColumnSelect
{
    void operator()(const InputQuery & query, const std::string & name) const
    {
        auto getWildcard = [] (const std::shared_ptr<SqlRowExpression> expression)
            -> std::shared_ptr<const WildcardExpression>
            {
                return std::dynamic_pointer_cast<const WildcardExpression>(expression);
            };

        auto getColumnExpression = [] (const std::shared_ptr<SqlRowExpression> expression)
            -> std::shared_ptr<const SelectColumnExpression>
            {
                return std::dynamic_pointer_cast<const SelectColumnExpression>(expression);
            };

        auto getComputedVariable = [] (const std::shared_ptr<SqlRowExpression> expression)
            -> std::shared_ptr<const ComputedVariable>
            {
                return std::dynamic_pointer_cast<const ComputedVariable>(expression);
            };

        auto getReadVariable = [] (const std::shared_ptr<SqlExpression> expression)
            -> std::shared_ptr<const ReadVariableExpression>
            {
                return std::dynamic_pointer_cast<const ReadVariableExpression>(expression);
            };

        auto getWithinExpression = [] (const std::shared_ptr<SqlExpression> expression)
            -> std::shared_ptr<const SelectWithinExpression>
            {
                return std::dynamic_pointer_cast<const SelectWithinExpression>(expression);
            };

        auto getIsTypeExpression = [] (const std::shared_ptr<SqlExpression> expression)
            -> std::shared_ptr<const IsTypeExpression>
            {
                return std::dynamic_pointer_cast<const IsTypeExpression>(expression);
            };

        auto getComparisonExpression = [] (const std::shared_ptr<SqlExpression> expression)
            -> std::shared_ptr<const ComparisonExpression>
            {
                return std::dynamic_pointer_cast<const ComparisonExpression>(expression);
            };

        auto getBooleanExpression = [] (const std::shared_ptr<SqlExpression> expression)
            -> std::shared_ptr<const BooleanOperatorExpression>
            {
                return std::dynamic_pointer_cast<const BooleanOperatorExpression>(expression);
            };

        auto getFunctionCallExpression = [] (const std::shared_ptr<SqlExpression> expression)
            -> std::shared_ptr<const FunctionCallWrapper>
            {
                return std::dynamic_pointer_cast<const FunctionCallWrapper>(expression);
            };

        auto getConstantExpression = [] (const std::shared_ptr<SqlExpression> expression)
            -> std::shared_ptr<const ConstantExpression>
            {
                return std::dynamic_pointer_cast<const ConstantExpression>(expression);
            };

        if (query.stm) {
            auto & select = query.stm->select;
            for (const auto & clause : select.clauses) {

                auto wildcard = getWildcard(clause);
                if (wildcard)
                    continue;

                auto columnExpression = getColumnExpression(clause);
                if (columnExpression)
                    continue;

                auto computedVariable = getComputedVariable(clause);
                if (computedVariable) {
                    auto readVariable = getReadVariable(computedVariable->expression);
                    if (readVariable)
                        continue;
                    // {x, y}
                    auto withinExpression = getWithinExpression(computedVariable->expression);
                    if (withinExpression)
                        continue;
                    // x is not null
                    auto isTypeExpression = getIsTypeExpression(computedVariable->expression);
                    if (isTypeExpression)
                        continue;
                    // x = 'true'
                    auto comparisonExpression = getComparisonExpression(computedVariable->expression);
                    if (comparisonExpression)
                        continue;
                    // NOT x
                    auto booleanExpression = getBooleanExpression(computedVariable->expression);
                    if (booleanExpression)
                        continue;
                    // function(args)[extract]
                    auto functionCallExpression = getFunctionCallExpression(computedVariable->expression);
                    if (functionCallExpression)
                        continue;
                     // 1.0
                    auto constantExpression = getConstantExpression(computedVariable->expression);
                    if (constantExpression)
                        continue;
                }

                throw ML::Exception(name +
                                    " only accepts wildcard and column names at " +
                                    clause->surface.rawString());
            }
        }
    }

    void operator()(const Optional<InputQuery> & query, const std::string & name) const
    {
        if (query) operator()(*query, name);
    }
};

inline bool containsNamedSubSelect(const InputQuery& query, const std::string& name)
{

    auto getComputedVariable = [] (const std::shared_ptr<SqlRowExpression> expression)
        -> std::shared_ptr<const ComputedVariable>
        {
            return std::dynamic_pointer_cast<const ComputedVariable>(expression);
        };

    if (query.stm) {
        auto & select = query.stm->select;
        for (const auto & clause : select.clauses) {

            auto computedVariable = getComputedVariable(clause);
            if (computedVariable && computedVariable->alias ==  name)
                return true;
        }
    }
    return false;
}

/**
 *  Ensure the select contains a row named "features" and a scalar named "label".
 *  FieldType must contain a SelectStatement named stm.
 */
struct FeaturesLabelSelect
{
    void operator()(const InputQuery & query, const std::string & name) const
    {
        if (!containsNamedSubSelect(query, "features") ||
            !containsNamedSubSelect(query, "label") )
            throw ML::Exception(name + " expects a row named 'features' and a scalar named 'label'");
    }

    void operator()(const Optional<InputQuery> & query, const std::string & name) const
    {
        if (query) operator()(*query, name);
    }
};

/**
 *  Ensure the select contains a scalar named "score" and a scalar named "label".
 *  FieldType must contain a SelectStatement named stm.
 */
struct ScoreLabelSelect
{
    void operator()(const InputQuery & query, const std::string & name) const
    {
        if (!containsNamedSubSelect(query, "score") ||
            !containsNamedSubSelect(query, "label") )
            throw ML::Exception(name + " expects a scalar named 'score' and a scalar named 'label'");
    }
};

/**
 *  Make sure that if a functionName is specified, a valid modelFileUrl
 *  is also specified.
 */
template<typename ConfigType>
std::function<void (ConfigType *, JsonParsingContext &)>
validateFunction()
{
    return [](ConfigType * cfg, JsonParsingContext & context) {
        if (!cfg->functionName.empty() &&
            !cfg->modelFileUrl.valid()) {
                throw ML::Exception(std::string(ConfigType::name) + " requires a valid "
                                    "modelFileUrl when specifying a functionName. "
                                    "modelFileUrl '" + cfg->modelFileUrl.toString()
                                    + "' is invalid.");
        }
    };
}

} // namespace MLDB
} // namespace Datacratic
