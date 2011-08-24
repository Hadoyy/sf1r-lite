/**
 * @file WorkerServer.h
 * @author Zhongxia Li
 * @date Jul 5, 2011
 * @brief 
 */
#ifndef WORKER_SERVER_H_
#define WORKER_SERVER_H_

#include <net/aggregator/JobWorker.h>
#include "WorkerService.h"

//#include <util/singleton.h>

#include <common/CollectionManager.h>
#include <controllers/CollectionHandler.h>
#include <bundles/index/IndexSearchService.h>

using namespace net::aggregator;

namespace sf1r
{

class KeywordSearchActionItem;
class KeywordSearchResult;

class WorkerServer : public JobWorker<WorkerServer>
{
private:
    boost::shared_ptr<WorkerService> workerService_;

public:
//    static WorkerServer* get()
//    {
//        return izenelib::util::Singleton<WorkerServer>::get();
//    }

    WorkerServer(const std::string& host, uint16_t port, unsigned int threadNum)
    : JobWorker<WorkerServer>(host, port, threadNum)
    {}

public:

    /**
     * Pre-process before dispatch (handle) a received request,
     * key is info such as collection, bundle name.
     */
    virtual bool preHandle(const std::string& key)
    {
        //cout << "#[WorkerServer::preHandle] key : " << key<<endl;

        if (!sf1r::SF1Config::get()->checkCollectionWorkerServer(key))
        {
            return false;
        }

        CollectionHandler* collectionHandler_ = CollectionManager::get()->findHandler(key);
        if (!collectionHandler_)
        {
            return false;
        }

        workerService_ = collectionHandler_->indexSearchService_->workerService_;

        return true;
    }

    /**
     * Handlers for processing received remote requests.
     */
    virtual void addHandlers()
    {
        ADD_WORKER_HANDLER_LIST_BEGIN( WorkerServer )

        ADD_WORKER_HANDLER( getDistSearchInfo )
        ADD_WORKER_HANDLER( getSearchResult )
        ADD_WORKER_HANDLER( getSummaryMiningResult )
        ADD_WORKER_HANDLER( getDocumentsByIds )
        ADD_WORKER_HANDLER( clickGroupLabel )
        // todo, add services

        ADD_WORKER_HANDLER_LIST_END()
    }

    /**
     * Publish worker services to remote procedure (as remote server)
     * @{
     */

    bool getDistSearchInfo(JobRequest& req)
    {
        WORKER_HANDLE_REQUEST_1_1(req, KeywordSearchActionItem, DistKeywordSearchInfo, workerService_, getDistSearchInfo)
        return true;
    }

    bool getSearchResult(JobRequest& req)
    {
        WORKER_HANDLE_REQUEST_1_1(req, KeywordSearchActionItem, DistKeywordSearchResult, workerService_, processGetSearchResult)
        return true;
    }

    bool getSummaryMiningResult(JobRequest& req)
    {
        WORKER_HANDLE_REQUEST_1_1(req, KeywordSearchActionItem, KeywordSearchResult, workerService_, processGetSummaryMiningResult)
        return true;
    }

    bool getDocumentsByIds(JobRequest& req)
    {
        WORKER_HANDLE_REQUEST_1_1(req, GetDocumentsByIdsActionItem, RawTextResultFromSIA, workerService_, getDocumentsByIds)
    	return true;
    }

    bool clickGroupLabel(JobRequest& req)
    {
        WORKER_HANDLE_REQUEST_1_1(req, ClickGroupLabelActionItem, bool, workerService_, clickGroupLabel)
        return true;
    }

    /** @} */
};

}

#endif /* WORKER_SERVER_H_ */
