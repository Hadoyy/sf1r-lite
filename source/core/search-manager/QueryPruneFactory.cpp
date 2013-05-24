#include "QueryPruneFactory.h"

namespace sf1r{

    QueryPruneFactory::QueryPruneFactory()
    {
        QueryPruneBase* and_qr = new AddQueryPrune();
        QueryPruneBase* qa_qr = new QAQueryPrune();
        QueryPruneMap_[QA_TRIGGER] = qa_qr;
        QueryPruneMap_[AND_TRIGGER] = and_qr;
    }

    QueryPruneFactory::~QueryPruneFactory()
    {
        for (std::map<QueryPruneType, QueryPruneBase*>::iterator i = QueryPruneMap_.begin(); 
            i != QueryPruneMap_.end(); ++i)
        {
            if (i->second)
            {
                delete i->second;
            }
        }
    }

    QueryPruneBase* QueryPruneFactory::getQueryPrune(QueryPruneType type) const
    {
        switch (type)
        {
            case QA_TRIGGER :
                return QueryPruneMap_.find(QA_TRIGGER)->second;

            case AND_TRIGGER:
                return QueryPruneMap_.find(AND_TRIGGER)->second;

            default:
                return NULL;
        }

    }
}