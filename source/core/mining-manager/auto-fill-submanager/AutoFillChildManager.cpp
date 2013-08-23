#include "AutoFillChildManager.h"


#include <mining-manager/query-correction-submanager/QueryCorrectionSubmanager.h>
#include <boost/algorithm/string/trim.hpp>
#include <idmlib/util/directory_switcher.h>
#include <am/vsynonym/QueryNormalize.h>
#include <util/scheduler.h>
#include <common/Utilities.h>

namespace sf1r
{
std::string AutoFillChildManager::system_resource_path_;
bool AutoFillChildManager::enableUpdateHitnum_;

AutoFillChildManager::AutoFillChildManager(bool isfromSCD)
    : isFromSCD_(isfromSCD)
{
    isUpdating_ = false;
    isUpdating_Wat_ = false;
    isIniting_ = false;

    updatelogdays_ = 1;
    alllogdays_ = 90;
    topN_ =  10;
    QN_ = new izenelib::am::QueryNormalize();
}

AutoFillChildManager::~AutoFillChildManager()
{
    if(!cronJobName_.empty())
        izenelib::util::Scheduler::removeJob(cronJobName_);
    closeDB();
    if (QN_ != NULL)
    {
        delete QN_;
    }
}

void AutoFillChildManager::SaveItem()
{
    fstream outItem;
    if(!ItemVector_.empty())
    {
        outItem.open(ItemPath_.c_str(), ios::out);
        if(outItem.is_open())
        {
            std::vector<ItemType>::iterator it;
            for(it = ItemVector_.begin(); it != ItemVector_.end(); it++)
            {
                outItem<<(*it).strItem_<<endl;
            }
            outItem.close();
        }
        else
        {
            //LOG(ERROR)<<"open saveItem file error"<<endl;
        }
    }
}

void AutoFillChildManager::LoadItem()
{
    ifstream in;
    in.open(ItemPath_.c_str(), ios::in);
    if(in.is_open())
    {
        while(!in.eof())
        {
            ItemType item;
            std::string temp;
            getline(in, temp);

            item.strItem_ = temp;
            item.offset_ = 0;
            ItemVector_.push_back(item);
        }
        in.close();
    }
    else
    {
        //LOG(ERROR)<<"open LoadItem file error"<<endl;
    }
}

void AutoFillChildManager::SaveWat()
{
    ofstream out1;
    out1.open(WatArrayPath_.c_str(), ios::out|ios::binary);
    if (out1.is_open())
    {
        wa_.Save(out1);
    }
}

bool AutoFillChildManager::LoadWat()
{
    ifstream in;
    in.open(WatArrayPath_.c_str(), ios::in|ios::binary);
    if (in.is_open())
    {
        wa_.Load(in);
        return true;
    }
    else
        return false;
}

bool AutoFillChildManager::PrepareForInit(const CollectionPath& collectionPath
        , const std::string& collectionName
        , const string& cronExpression
        , const string& instanceName
        , bool & isBuildFromLeveldb)
{
    AutofillPath_ = collectionPath.getQueryDataPath() + "/autofill" + "/" + instanceName;
    collectionName_ = collectionName;
    cronJobName_ = "Autofill-cron-" + collectionName_ + "-" + instanceName;
    if (!boost::filesystem::is_directory(AutofillPath_))
    {
        boost::filesystem::create_directories(AutofillPath_);
    }
    boost::mutex::scoped_try_lock lock(buildCollectionMutex_);
    leveldbPath_ = AutofillPath_ + "/leveldb";
    ItemdbPath_ = AutofillPath_ + "/itemdb";
    std::string IDPath = AutofillPath_ + "/id/";
    ItemPath_ = leveldbPath_ + "/Itemlist.list";
    WatArrayPath_ = leveldbPath_ + "/wavelet.tree";
    SCDLogPath_ = AutofillPath_ +"/SCDLog";
    SCDDIC_ = collectionPath.getScdPath() + "autofill";

    std::string dictionaryFile = system_resource_path_;
    if(dictionaryFile.rfind("/") != dictionaryFile.length()-1)
    {
        dictionaryFile += "/qn/QUERYNORMALIZE";
    }
    else
    {
        dictionaryFile += "qn/QUERYNORMALIZE";
    }

    QN_->load(dictionaryFile);
    try
    {
        if (!boost::filesystem::is_directory(leveldbPath_))
        {
            boost::filesystem::create_directories(leveldbPath_);

            if (!boost::filesystem::is_directory(ItemdbPath_))
            {
                boost::filesystem::create_directories(ItemdbPath_);
            }

            if (!boost::filesystem::is_directory(IDPath))
            {
                boost::filesystem::create_directories(IDPath);
            }

            if (!boost::filesystem::is_directory(SCDDIC_))
            {
                boost::filesystem::create_directories(SCDDIC_);
            }
        }
        else
        {
            if (boost::filesystem::is_directory(IDPath) && boost::filesystem::is_directory(ItemdbPath_) && boost::filesystem::exists(ItemPath_))
            {
                isBuildFromLeveldb = true;
            }
        }
    }
    catch (boost::filesystem::filesystem_error& e)
    {
        LOG(ERROR)<<"Path does not exist. Path "<<leveldbPath_;
    }

    idManager_.reset(new IDManger(IDPath));

    if (cronExpression_.setExpression(cronExpression))
    {
        bool result = izenelib::util::Scheduler::addJob(cronJobName_,
                      60*1000, // each minute
                      0, // start from now
                      boost::bind(&AutoFillChildManager::updateAutoFill, this, _1));
        if (! result)
            LOG(ERROR) << "failed in izenelib::util::Scheduler::addJob(), cron job name: " << cronJobName_;
        else
            LOG(INFO) << "create cron job : " << cronJobName_<<" expression: "<<cronExpression;
    }

    if(!openDB(leveldbPath_, ItemdbPath_))
    {
        LOG (ERROR) << "open leveldb wrong..." << endl;
        return false;
    }
    return true;
}

bool AutoFillChildManager::Init_ForTest(const CollectionPath& collectionPath
                                        , const std::string& collectionName
                                        , const string& cronExpression
                                        , const string& instanceName
                                        , bool & isBuildFromLeveldb
                                        , std::vector<UserQuery>& query_records)
{
    isBuildFromLeveldb = false;
    if (!PrepareForInit(collectionPath, collectionName, cronExpression, instanceName, isBuildFromLeveldb))
        return false;

    if(isBuildFromLeveldb)
    {
        InitWhileHaveLeveldb();
    }
    else
    {
        if(!InitFromLog_ForTest(query_records))
        {
            return false;
        }
    }
    return true;
}

bool AutoFillChildManager::Init(const CollectionPath& collectionPath, const std::string& collectionName, const string& cronExpression, const string& instanceName)
{
    bool isBuildFromLeveldb = false;
    if (!PrepareForInit(collectionPath, collectionName, cronExpression, instanceName, isBuildFromLeveldb))
        return false;

    if(isBuildFromLeveldb)
    {
        InitWhileHaveLeveldb();
    }
    else
    {
        if(!RealInit())
        {
            return false;
        }
    }
    isIniting_ = false;
    return true;
}

bool AutoFillChildManager::InitWhileHaveLeveldb()
{
    LoadItem();
    buildItemVector();
    isUpdating_Wat_ = true;
    if(!LoadWat())
    {
        buildWat_array(true);
    }
    else
    {
        LOG (INFO) <<"Autofill Started from leveldb ..."<<endl;
        //buildDbItem();
    }
    isUpdating_Wat_ = false;
    return true;
}

bool AutoFillChildManager::RealInit()
{
    isIniting_ = true;
    if(isFromSCD_)
    {
        if(!InitFromSCD())
            return false;
    }
    else
    {
        if(!InitFromLog())
            return false;
    }
    isIniting_ = false;
    return true;
}

void AutoFillChildManager::LoadSCDLog()
{
    ifstream in;
    in.open(SCDLogPath_.c_str(), ios::in);
    if(in.is_open())
    {
        while(!in.eof())
        {
            std::string temp;
            getline(in, temp);
            SCDHaveDone_.push_back(temp);
        }
        in.close();
    }
    else
    {
        LOG(ERROR)<<"open LoadSCDLog file error"<<endl;
    }
}

void AutoFillChildManager::SaveSCDLog()
{
    fstream out1;
    if(!SCDHaveDone_.empty())
    {
        out1.open(SCDLogPath_.c_str(), ios::out);
        if(out1.is_open())
        {
            for(vector<string>::iterator it =SCDHaveDone_.begin(); it != SCDHaveDone_.end(); it++)
            {
                out1<<(*it)<<endl;
            }
            out1.close();
        }
        else
        {
            LOG(ERROR)<<"open SaveSCDLog file error"<<endl;
        }
    }
}

bool AutoFillChildManager::InitFromSCD()
{
    if(boost::filesystem::exists(SCDLogPath_))
    {
        LoadSCDLog();
    }
    return true;
}

void AutoFillChildManager::updateFromSCD()
{
    std::list<ItemValueType> querylist;
    const bfs::directory_iterator kItrEnd;

    if (!boost::filesystem::is_directory(SCDDIC_))
    {
        return;
    }

    for (bfs::directory_iterator itr(SCDDIC_); itr != kItrEnd; ++itr)
    {
        if (bfs::is_regular_file(itr->status()))
        {
            std::string fileName = itr->path().string();
            std::ifstream in;
            in.open(fileName.c_str(), ios::in);
            if (in.good())
            {
                SCDHaveDone_.push_back(fileName);
                std::string temp;
                std::string title = "<Title>";
                while(!in.eof())
                {
                    getline(in, temp);
                    if(temp.substr(0, title.size()) == title)
                    {
                        izenelib::util::UString UStringQuery_(temp.substr(title.size()),izenelib::util::UString::UTF_8);
                        querylist.push_back(boost::make_tuple(0, 0, UStringQuery_));
                    }
                }
                in.close();
            }
            else
            {
                in.close();
                SaveSCDLog();
                return;
            }
        }
    }
    if(!querylist.empty())
    {
        LoadItem();
        buildIndex(querylist);
        SaveSCDLog();

        bfs::path bkDir = bfs::path(SCDDIC_) / "backup";
        bfs::create_directory(bkDir);

        for (std::vector<std::string>::iterator scd_it = SCDHaveDone_.begin(); scd_it != SCDHaveDone_.end(); ++scd_it)
        {
            try
            {
                bfs::rename(*scd_it, bkDir / bfs::path(*scd_it).filename());
            }
            catch (bfs::filesystem_error& e)
            {
                LOG(WARNING) << "exception in rename file " << *scd_it << ": " << e.what();
            }
        }
    }
}

bool AutoFillChildManager::InitFromLog_ForTest(std::vector<UserQuery>& query_records)
{
    list<ItemValueType> querylist;
    std::vector<UserQuery>::const_iterator it = query_records.begin();
    for (; it != query_records.end(); ++it)
    {
        bool isNormalString = true;
        std::string query = it->getQuery();
        QN_->query_Normalize(query);
        izenelib::util::UString UStringQuery_(query, izenelib::util::UString::UTF_8);
        for (unsigned int i = 0; i < UStringQuery_.length(); ++i)
        {
            if (UStringQuery_.isPunctuationChar(i))
            {
                isNormalString = false;
                break;
            }
        }
        if (isNormalString)
        {
            querylist.push_back(boost::make_tuple(it->getCount(), it->getHitDocsNum(), UStringQuery_));
        }
    }
    buildIndex(querylist);
    return true;
}

bool AutoFillChildManager::InitFromLog()
{
    boost::posix_time::ptime time_now = boost::posix_time::microsec_clock::local_time();
    boost::posix_time::ptime p = time_now - boost::gregorian::days(alllogdays_);
    std::string time_string = boost::posix_time::to_iso_string(p);
    std::vector<UserQuery> query_records;
    LogAnalysis::getRecentKeywordFreqList(collectionName_, time_string, query_records,AutoFillChildManager::enableUpdateHitnum_);

    list<ItemValueType> querylist;
    std::vector<UserQuery>::const_iterator it = query_records.begin();

    int count= 0;
    for (; it != query_records.end(); ++it)
    {
        count++;
        bool isNormalString = true;
        std::string query = it->getQuery();
        if (QueryNormalizer::get()->countCharNum(query) > 10)
            continue;
        QN_->query_Normalize(query);
        izenelib::util::UString UStringQuery_(query, izenelib::util::UString::UTF_8);
        for (unsigned int i = 0; i < UStringQuery_.length(); ++i)
        {
            if (UStringQuery_.isPunctuationChar(i))
            {
                isNormalString = false;
                break;
            }
        }
        if (isNormalString)
        {
            querylist.push_back(boost::make_tuple(it->getCount(), it->getHitDocsNum(), UStringQuery_));
        }
    }
    buildIndex(querylist);
    return true;
}

bool AutoFillChildManager::openDB(std::string Tablepath, std::string Itempath)
{
    return dbTable_.open(Tablepath) && dbItem_.open(Itempath);
}

void AutoFillChildManager::closeDB()
{
    dbTable_.close();
    dbItem_.close();
}

bool AutoFillChildManager::buildDbIndex(const std::list<QueryType>& queryList)
{
    std::list<QueryType>::const_iterator it;
    std::vector<std::pair<std::string, std::string> > similarList;
    vector<PrefixQueryType> thousandpair;
    unsigned int querynum = 0;
    for(it = queryList.begin(); it != queryList.end(); it++)
    {
        querynum++;
        std::vector<izenelib::util::UString> pinyins;

        FREQ_TYPE freq = (*it).freq_;
        uint32_t HitNum = (*it).HitNum_;
        std::string strT = (*it).strQuery_;

        boost::algorithm::trim(strT);
        std::transform(strT.begin(), strT.end(), strT.begin(), ::tolower);

        izenelib::util::UString UStringQuery(strT,izenelib::util::UString::UTF_8);
        QueryCorrectionSubmanager::getInstance().getRelativeList(UStringQuery, pinyins);

        std::vector<izenelib::util::UString>::const_iterator itv = pinyins.begin();
        for( ; itv != pinyins.end(); itv++)
        {
            PrefixQueryType prefix;
            std::string pinyin;
            (*itv).convertString(pinyin, izenelib::util::UString::UTF_8);
            if(pinyin.length() > 30)
            {
                continue;
            }
            prefix.init(strT, pinyin, freq, HitNum);
            thousandpair.push_back(prefix);
        }
        if(querynum%10000==0)
        {
            buildDbIndexForEveryThousand(thousandpair, similarList);
            thousandpair.clear();
        }
    }
    buildDbIndexForEveryThousand(thousandpair, similarList);
    thousandpair.clear();

    dealWithSimilar(similarList);
    buildItemVector();
    return true;
}

void AutoFillChildManager::buildDbIndexForEveryThousand(vector<PrefixQueryType> thousandpair, std::vector<std::pair<string,string> >& similarList)
{
    sort( thousandpair.begin(), thousandpair.end());
    std::string prefixtemp = "";
    std::vector<QueryType> sameprefix;
    for(unsigned int i = 0; i < thousandpair.size(); i++)
    {
        if(thousandpair[i].prefix_ == prefixtemp)
        {
            sameprefix.push_back(thousandpair[i].getQueryType());
        }
        else
        {
            buildDbIndexForEach(make_pair(prefixtemp, sameprefix), similarList);
            sameprefix.clear();
            sameprefix.push_back(thousandpair[i].getQueryType());
        }
        prefixtemp = thousandpair[i].prefix_;
    }
    buildDbIndexForEach(make_pair(prefixtemp, sameprefix), similarList);
    sameprefix.clear();
}

void AutoFillChildManager::buildDbIndexForEach( std::pair<std::string,std::vector<QueryType> > eachprefix, std::vector<std::pair<string,string> >& similarList)
{
    bool Similar = false;
    bool check = false;
    std::string pinyin, value, nospacepinyin, withspacepinyin;
    std::vector<QueryType> sameprefix = eachprefix.second;
    pinyin = eachprefix.first;
    if (pinyin.length() > 0 && pinyin[0] != ' ')
    {
        buildItemList(pinyin);
    }
    withspacepinyin = pinyin;
    nospacepinyin = pinyin;
    if (nospacepinyin!=pinyin)
    {
        if (nospacepinyin.length() > 0)
        {
            Similar=true;
            withspacepinyin=pinyin;
            pinyin=nospacepinyin;
            check=true;
        }
    }
    if (Similar == true)
    {
        similarList.push_back(std::make_pair(nospacepinyin,withspacepinyin));
    }

    dbTable_.get_item(pinyin, value);
    dbTable_.delete_item(pinyin);
    queryover d1;
    queryequal d2;
    std::vector<QueryType>  havedone = valueToQueryTypeVector(value);
    sort( sameprefix.begin(), sameprefix.end(), d1);
    sort( havedone.begin(), havedone.end(), d1);
    vector<QueryTypeToDeleteDup> qtddvec;
    std::vector<QueryType>::iterator SubstrIter_a=sameprefix.begin(),SubstrIter_b=havedone.begin(),SubstrIter_a_end=sameprefix.end(),SubstrIter_b_end=havedone.end();
    std::vector<QueryType>::iterator LastQueryType;
    bool begin=true;
    while(SubstrIter_a < SubstrIter_a_end || SubstrIter_b < SubstrIter_b_end)
    {
        if(SubstrIter_a==SubstrIter_a_end )
        {
            if(!begin)
            {
                if(d2(*LastQueryType,*SubstrIter_b))
                {
                    LastQueryType->freq_+=SubstrIter_b->freq_;
                }
                if(LastQueryType->HitNum_>0)
                {
                    QueryTypeToDeleteDup qtdd;
                    qtdd.initfrom(*LastQueryType);
                    qtddvec.push_back(qtdd);
                }
            }
            LastQueryType=SubstrIter_b;
            begin=false;
            ++SubstrIter_b;
            continue;
        }
        else if(SubstrIter_b==SubstrIter_b_end)
        {
            if(!begin)
            {
                if(d2(*LastQueryType,*SubstrIter_a))
                {
                    LastQueryType->freq_+=SubstrIter_a->freq_;
                }
                if(LastQueryType->HitNum_>0)
                {
                    QueryTypeToDeleteDup qtdd;
                    qtdd.initfrom(*LastQueryType);
                    qtddvec.push_back(qtdd);
                }
            }
            LastQueryType=SubstrIter_a;
            begin=false;
            ++SubstrIter_a;
            continue;
        }
        if((*SubstrIter_a).strQuery_> (*SubstrIter_b).strQuery_ )
        {
            if(!begin)
            {
                if(d2(*LastQueryType,*SubstrIter_a))
                {
                    LastQueryType->freq_+=SubstrIter_a->freq_;
                    LastQueryType->HitNum_=SubstrIter_a->HitNum_;
                }
                if(LastQueryType->HitNum_>0)
                {
                    QueryTypeToDeleteDup qtdd;
                    qtdd.initfrom(*LastQueryType);
                    qtddvec.push_back(qtdd);
                }
            }
            LastQueryType=SubstrIter_a;
            begin=false;
            ++SubstrIter_a;
        }
        else
        {
            if(!begin)
            {
                if(d2(*LastQueryType,*SubstrIter_b))
                {
                    LastQueryType->freq_+=SubstrIter_b->freq_;
                }
                if(LastQueryType->HitNum_>0)
                {
                    QueryTypeToDeleteDup qtdd;
                    qtdd.initfrom(*LastQueryType);
                    qtddvec.push_back(qtdd);
                }
            }
            LastQueryType=SubstrIter_b;
            begin=false;
            ++SubstrIter_b;
        }
    }
    if(!begin)
    {
        if(LastQueryType->HitNum_>0)
        {
            QueryTypeToDeleteDup qtdd;
            qtdd.initfrom(*LastQueryType);
            qtddvec.push_back(qtdd);
        }
    }
    sort( qtddvec.begin(),qtddvec.end());
    qtddvec.erase(std::unique(qtddvec.begin(), qtddvec.end()), qtddvec.end());
    sameprefix.clear();
    for (unsigned i=0; i<qtddvec.size(); i++)
    {
        sameprefix.push_back(qtddvec[i].qt_);
    }
    sort(sameprefix.begin(), sameprefix.end());
    value = "";
    for (unsigned int i = 0; i < sameprefix.size(); i++)
    {
        FREQ_TYPE freq = sameprefix[i].freq_;
        uint32_t HitNum = sameprefix[i]. HitNum_;
        std::string strT = sameprefix[i].strQuery_;
        if(value.length() == 0)
        {
            ValueType newValue;
            std::string valuet, firstvalue;
            firstvalue = "0000";
            newValue.toValueType(strT, freq, HitNum);
            newValue.toString(valuet);
            firstvalue.append(valuet);
            value=firstvalue;
        }
        else
        {
            ValueType newValue;
            newValue.toValueType(strT, freq, HitNum);
            std::string newValueStr;
            newValue.toString(newValueStr);
            value.append(newValueStr);
        }
    }
    if (!dbTable_.add_item(pinyin, value));
}

std::vector<QueryType> AutoFillChildManager::valueToQueryTypeVector(std::string value)
{
    std::vector<QueryType> querytemp;
    uint32_t len = value.length();
    const char* str = value.data() + TOPN_LEN;
    uint32_t offset = TOPN_LEN;

    while(offset < len)
    {
        ValueType newValue;
        newValue.setValueType(str);
        querytemp.push_back(newValue.getQueryType());
        offset += *(uint32_t*)str;
        str += *(uint32_t*)str;
    }
    return querytemp;
}
void AutoFillChildManager::dealWithSimilar(std::vector<std::pair<string,string> >& similarList)
{
    std::sort(similarList.begin(), similarList.end());
    std::vector<std::pair<string,string> >::iterator iter = std::unique(similarList.begin(), similarList.end());
    similarList.erase(iter, similarList.end());
    std::string value;
    for(unsigned i=0; i<similarList.size(); i++)
    {
        dbTable_.get_item(similarList[i].first, value);
        dbTable_.add_item(similarList[i].second,value);
        buildItemList(similarList[i].second);
        buildItemList(similarList[i].first);
    }
}

void AutoFillChildManager::buildItemList(std::string key)
{
    ItemType item;
    item.strItem_ = key;
    item.offset_ = 0;
    ItemVector_.push_back(item);
}

void AutoFillChildManager::buildItemVector()
{
    std::sort(ItemVector_.begin(), ItemVector_.end());
    vector<ItemType>::iterator iter = unique(ItemVector_.begin(), ItemVector_.end());
    ItemVector_.erase(iter, ItemVector_.end());
}

void AutoFillChildManager::buildTopNDbTable(std::string &value, const uint32_t offset)//compare with the least one of topN
{
    std::string MinFreqstr= value.substr(0, TOPN_LEN);
    std::string str0=MinFreqstr;
    uint32_t MinFreq;
    if(MinFreqstr == "0000")
    {
        MinFreq = 0;
    }
    else
    {
        MinFreq = *(uint32_t*)(MinFreqstr.data());
    }

    std::string sizestr = value.substr(offset, FREQ_TYPE_LEN);
    uint32_t sizeinoffset = *(uint32_t*)(sizestr.data());
    std::string freqstr = value.substr(offset + sizeinoffset - HITNUM_LEN  - UINT32_LEN, FREQ_TYPE_LEN);
    uint32_t freq = *(uint32_t*)(freqstr.data());

    std::string singlefreq;
    uint32_t size = 0;
    uint32_t ThisPos = TOPN_LEN;
    uint32_t ThisFreq = freq + 1;
    uint32_t ThisOrder = 0;
    if(freq < MinFreq)
    {
        //
    }
    else
    {
        while(ThisPos < offset && freq < ThisFreq)
        {
            ThisOrder++;
            ThisPos = ThisPos+size;
            sizestr = value.substr(ThisPos, FREQ_TYPE_LEN );
            size = *(uint32_t*)(sizestr.data());
            singlefreq = value.substr(ThisPos + size - HITNUM_LEN  - UINT32_LEN, FREQ_TYPE_LEN);
            ThisFreq = *(uint32_t*)(singlefreq.data());
        }
        uint32_t ThisPos2 = ThisPos;

        while( ThisOrder <= topN_-1 && ThisPos2 < value.length())
        {
            ThisOrder++;
            ThisPos2 = ThisPos2+size;
            sizestr = value.substr( ThisPos2 , 4 );
            size = *(uint32_t*)(sizestr.data());
        }

        std::string str1,str2,str3,str4;
        if(ThisPos2<value.length())
        {
            str0 = value.substr(ThisPos2 + size - HITNUM_LEN  - UINT32_LEN, FREQ_TYPE_LEN);
        }
        str1 = value.substr(TOPN_LEN, ThisPos - TOPN_LEN);
        str2 = value.substr(ThisPos, offset-ThisPos );
        str3 = value.substr(offset, sizeinoffset );
        str4 = value.substr(offset + sizeinoffset);
        value = str0 + str1 + str3 + str2 + str4;
    }
}

bool AutoFillChildManager::getAutoFillListFromOffset(uint64_t OffsetStart, uint64_t OffsetEnd, std::vector<std::pair<izenelib::util::UString,uint32_t> >& list)
{
    izenelib::util::UString tempUString;
    std::string ValueStr;
    uint32_t ValueID = 0;
    for(uint64_t i = OffsetStart; i <= OffsetEnd; i++)
    {
        ValueID = (uint32_t)wa_.Lookup(i);
        idManager_->getDocNameByDocId(ValueID, ValueStr);
        std::size_t pos = ValueStr.find("/");
        uint32_t HitNum ;
        string strquery;
        string HitNumStr;
        if(pos == 0)
            continue;
        std::string portStr;
        if(pos == std::string::npos)
        {
            continue;
        }
        else
        {
            HitNumStr = ValueStr.substr(0, pos);
            strquery = ValueStr.substr(pos + 1);
        }
        HitNum=boost::lexical_cast<int>(HitNumStr);
        izenelib::util::UString Ustr(strquery, izenelib::util::UString::UTF_8);
        list.push_back(std::make_pair(Ustr, HitNum));
    }
    return true;
}

bool AutoFillChildManager::getAutoFillListFromWat(const izenelib::util::UString& queryOrgin, std::vector<std::pair<izenelib::util::UString,uint32_t> >& list)
{
    izenelib::util::UString query = izenelib::util::Algorithm<izenelib::util::UString>::trim1(queryOrgin);
    if (query.length() == 0)
        return false;

    izenelib::util::UString tempQuery;
    bool ret = false;
    uint64_t OffsetStart;
    uint64_t OffsetEnd;
    std::string strQuery;
    bool HaveSearched = false;
    query.convertString(strQuery, izenelib::util::UString::UTF_8);

    if (query.isAllChineseChar()) ///Chinese
    {
        HaveSearched = getOffset(strQuery, OffsetStart, OffsetEnd);
        if(HaveSearched)
        {
            ret = getAutoFillListFromOffset(OffsetStart, OffsetEnd, list);
        }
    }
    else if (izenelib::util::ustring_tool::processKoreanDecomposerWithCharacterCheck<
             izenelib::util::UString>(query, tempQuery)) ///Korean
    {
        HaveSearched = getOffset(strQuery, OffsetStart, OffsetEnd);
        if(HaveSearched)
        {
            ret = getAutoFillListFromOffset(OffsetStart, OffsetEnd, list);
        }
    }
    else /// Normal
    {
        if (query.includeChineseChar())
        {
            HaveSearched = getOffset(strQuery, OffsetStart, OffsetEnd);
            if(HaveSearched)
            {
                ret = getAutoFillListFromOffset(OffsetStart, OffsetEnd, list);
            }
            else//Hybrid
            {
                std::vector<izenelib::util::UString> pinyins;
                QueryCorrectionSubmanager::getInstance().getPinyin2(query, pinyins);
                std::vector<std::pair<izenelib::util::UString,uint32_t> > tempList;
                for (uint32_t j = 0; j < pinyins.size(); j++)
                {
                    tempList.clear();
                    pinyins[j].convertString(strQuery, izenelib::util::UString::UTF_8);
                    HaveSearched = getOffset(strQuery, OffsetStart, OffsetEnd);
                    if(HaveSearched)
                    {
                        getAutoFillListFromOffset(OffsetStart, OffsetEnd, tempList);
                        if(!tempList.empty())
                        {
                            list.insert(list.end(), tempList.begin(), tempList.end());
                        }
                    }
                }

                query.filter(list);
                std::vector<std::pair<izenelib::util::UString,uint32_t> >::iterator iter;
                for (iter=list.begin(); iter!=list.end(); )
                {
                    if((*iter).first==query)
                    {
                        iter=list.erase(iter);
                    }
                    else
                        iter++;
                }
                query.KeepOrderDuplicateFilter(list);
            }
        }
        else
        {
            HaveSearched = getOffset(strQuery, OffsetStart, OffsetEnd);
            if(HaveSearched)
            {
                ret = getAutoFillListFromOffset(OffsetStart, OffsetEnd, list);
            }
        }
    }
    std::vector<std::pair<izenelib::util::UString,uint32_t> >::iterator iter=list.begin();
    if(query.includeChineseChar()&&list.size()<topN_)
    {
        std::vector<izenelib::util::UString> pinyins;
        QueryCorrectionSubmanager::getInstance().getPinyin2(query, pinyins);
        std::vector<std::pair<izenelib::util::UString,uint32_t> > tempList;
        for (uint32_t j = 0; j < pinyins.size(); j++)
        {
            tempList.clear();
            pinyins[j].convertString(strQuery, izenelib::util::UString::UTF_8);
            HaveSearched = getOffset(strQuery, OffsetStart, OffsetEnd);
            if(HaveSearched)
            {
                getAutoFillListFromOffset(OffsetStart, OffsetEnd, tempList);
                if(!tempList.empty())
                {
                    list.insert(list.end(), tempList.begin(), tempList.end());
                }
            }
        }
        query.FuzzyFilter(list);
        query.KeepOrderDuplicateFilter(list);
    }

    if(!list.empty())
    {
        for (iter=list.begin(); iter!=list.end(); )
        {
            if( (*iter).first==query)
            {
                iter=list.erase(iter);
            }
            else
                iter++;
        }
    }

    string strQueryOrgin=strQuery;

    if(strQueryOrgin!=strQuery)
    {
        std::vector<std::pair<izenelib::util::UString, uint32_t> > tempList;
        for(unsigned i=0; i<list.size(); i++)
        {
            std::string tempString;
            list[i].first.convertString(tempString, izenelib::util::UString::UTF_8);

            boost::algorithm::replace_all(tempString,strQuery,strQueryOrgin);
            izenelib::util::UString tempUString(tempString, izenelib::util::UString::UTF_8);
            tempList.push_back(std::make_pair(tempUString, list[i].second));

        }
        list.clear();
        list.insert(list.end(), tempList.begin(), tempList.end());
    }

    ret = !list.empty();
    return ret;
}

bool AutoFillChildManager::getAutoFillListFromDbTable(const izenelib::util::UString& queryOrgin, std::vector<std::pair<izenelib::util::UString, uint32_t> >& list)
{
    izenelib::util::UString query = izenelib::util::Algorithm<izenelib::util::UString>::trim1(queryOrgin);
    std::string value;
    dbTable_.get_item(query, value);
    const char* valuestr = value.data() + TOPN_LEN;
    std::string sizestr;
    std::string singlevalue;
    uint32_t size = 0;
    uint32_t count = 0;
    uint32_t NextStartPos = TOPN_LEN;
    uint32_t HitNum = 0;
    bool ret = false;
    while (NextStartPos < value.length())
    {
        if(count >= topN_)
            break;
        size = *(uint32_t*)valuestr;
        singlevalue = value.substr(NextStartPos + UINT32_LEN, size - UINT32_LEN - HITNUM_LEN- FREQ_TYPE_LEN);
        HitNum = *(uint32_t*)(valuestr + size - FREQ_TYPE_LEN);
        izenelib::util::UString  tempsinglevalue(singlevalue, izenelib::util::UString::UTF_8);
        list.push_back(std::make_pair(tempsinglevalue, HitNum));
        NextStartPos = NextStartPos + size;
        valuestr += size;
        count++;
        ret = true;
    }
    return ret;
}

bool AutoFillChildManager::getAutoFillList(const izenelib::util::UString& query, std::vector<std::pair<izenelib::util::UString, uint32_t> >& list)
{
    std::string strquery;
    query.convertString(strquery, izenelib::util::UString::UTF_8);
    QN_->query_Normalize(strquery);
    if(strquery.empty())
        return true;
    izenelib::util::UString queryLow(strquery, izenelib::util::UString::UTF_8);

    if(!isIniting_)
    {
        if( isUpdating_ && isUpdating_Wat_)
            return getAutoFillListFromDbTable(queryLow , list);
        else
            return getAutoFillListFromWat(queryLow, list);
    }
    else
    {
        if( isUpdating_Wat_)
        {
            return getAutoFillListFromDbTable(queryLow , list);
        }
    }
    return true;
}

void AutoFillChildManager::buildWat_array(bool _fromleveldb)
{
    vector<uint64_t> A;
    std::vector<ItemType>::iterator it;
    std::string itemValue, value, IDstring, oldIDstring;
    uint32_t ID = 0;
    uint32_t offsettmp = 0;
    //if(!_fromleveldb)
    dbItem_.clear();
    oldIDstring = "";
    for(it = ItemVector_.begin(); it != ItemVector_.end(); it++)
    {
        itemValue = (*it).strItem_;
        dbTable_.get_item(itemValue, value);
        const char* str = value.data() + TOPN_LEN;
        uint32_t len = value.length();
        uint32_t count = 0;
        for(uint32_t offset = TOPN_LEN; offset < len;)
        {
            if(count >= topN_)
                break;
            ValueType newValue;
            newValue.setValueType(str);
            /*
                        if(newValue.HitNum_>0)
                        {
                            offset += *(uint32_t*)str;
                            str += *(uint32_t*)str;
                            continue;
                        }
            */
            newValue.getHitString(IDstring);
            if (IDstring != oldIDstring)
            {
                idManager_->getDocIdByDocName(IDstring, ID);
                oldIDstring = IDstring;
            }
            A.push_back((uint64_t)ID);
            count++;
            offset += *(uint32_t*)str;
            str += *(uint32_t*)str;
        }
        (*it).offset_ = offsettmp;
        offsettmp += count;
        {
            std::string offsetstring = boost::lexical_cast<string>((*it).offset_) + "/" + boost::lexical_cast<string>(count);
            dbItem_.add_item(itemValue, offsetstring);
        }
    }
    wa_.Init(A);
    SaveItem();
    SaveWat();
    vector<ItemType>().swap(ItemVector_);
}

void AutoFillChildManager::buildDbItem()
{
    std::vector<ItemType>::iterator it;
    std::string itemValue, value, IDstring;
    uint32_t offsettmp = 0;
    dbItem_.clear();
    for(it = ItemVector_.begin(); it != ItemVector_.end(); it++)
    {
        itemValue = (*it).strItem_;
        dbTable_.get_item(itemValue, value);
        const char* str = value.data() + TOPN_LEN;
        uint32_t len = value.length();
        uint32_t count = 0;
        for(uint32_t offset = TOPN_LEN; offset < len;)
        {
            if(count >= topN_)
                break;
            ValueType newValue;
            newValue.setValueType(str);
            newValue.getHitString(IDstring);
            count++;
            offset += *(uint32_t*)str;
            str += *(uint32_t*)str;
        }
        (*it).offset_ = offsettmp;
        offsettmp += count;
        {
            std::string offsetstring = boost::lexical_cast<string>((*it).offset_) + "/" + boost::lexical_cast<string>(count);
            dbItem_.add_item(itemValue, offsetstring);
        }
    }
    SaveItem();
    vector<ItemType>().swap(ItemVector_);
}

bool AutoFillChildManager::getOffset(const std::string& query, uint64_t& OffsetStart, uint64_t& OffsetEnd)
{
    OffsetStart = 1;
    OffsetEnd = 0;
    std::string offsetstr = "";
    dbItem_.get_item(query, offsetstr);
    if(offsetstr.length() > 0)
    {
        std::size_t pos = offsetstr.find("/");
        std::string offsetbegin = offsetstr.substr(0, pos);
        std::string offsetcount = offsetstr.substr(pos + 1);
        try
        {
            OffsetStart = boost::lexical_cast<uint32_t>(offsetbegin);
            OffsetEnd = boost::lexical_cast<uint32_t>(offsetcount) + OffsetStart - 1;
        }
        catch(boost::bad_lexical_cast& e)
        {
        }
    }
    return true;
}

void AutoFillChildManager::updateAutoFill(int calltype)
{
    if (cronExpression_.matches_now())
    {
        boost::mutex::scoped_try_lock lock(buildCollectionMutex_);

        if (lock.owns_lock() == false)
        {
            LOG(INFO) << "Autofill Is already initing ....";
            return;
        }
        isUpdating_ = true;
        if(!isFromSCD_)
        {
            updateFromLog();
        }
        else
        {
            updateFromSCD();
        }
        isUpdating_Wat_ = false;
        isUpdating_ = false;
    }
}

void AutoFillChildManager::updateFromLog_ForTest(std::vector<UserQuery>& query_records)
{
    list<QueryType> querylist;
    std::vector<UserQuery>::const_iterator it = query_records.begin();
    for ( ; it != query_records.end(); ++it)
    {
        QueryType TempQuery;
        bool isNormalString = true;
        std::string query = it->getQuery();
        QN_->query_Normalize(query);
        izenelib::util::UString UStringQuery_(query, izenelib::util::UString::UTF_8);
        for (unsigned int i = 0; i < UStringQuery_.length(); ++i)
        {
            if (UStringQuery_.isPunctuationChar(i))
            {
                isNormalString = false;
                break;
            }
        }
        if (isNormalString)
        {
            TempQuery.strQuery_ = it->getQuery();
            TempQuery.freq_ = it->getCount();
            TempQuery.HitNum_ = it->getHitDocsNum();
            querylist.push_back(TempQuery );
        }
    }
    LoadItem();
    buildItemVector();
    buildDbIndex(querylist);
    isUpdating_Wat_ = true;
    wa_.Clear();
    buildWat_array(false);
}

void AutoFillChildManager::updateFromLog()
{
    boost::posix_time::ptime time_now = boost::posix_time::microsec_clock::local_time();
    boost::posix_time::ptime p = time_now - boost::gregorian::days(updatelogdays_);
    std::string time_string = boost::posix_time::to_iso_string(p);
    std::vector<UserQuery> query_records;

    LogAnalysis::getRecentKeywordFreqList(collectionName_, time_string, query_records,AutoFillChildManager::enableUpdateHitnum_);

    list<QueryType> querylist;
    std::vector<UserQuery>::const_iterator it = query_records.begin();
    for ( ; it != query_records.end(); ++it)
    {
        QueryType TempQuery;
        bool isNormalString = true;
        std::string query = it->getQuery();
        if (QueryNormalizer::get()->countCharNum(query) > 10)
            continue;
        QN_->query_Normalize(query);
        izenelib::util::UString UStringQuery_(query, izenelib::util::UString::UTF_8);
        for (unsigned int i = 0; i < UStringQuery_.length(); ++i)
        {
            if (UStringQuery_.isPunctuationChar(i))
            {
                isNormalString = false;
                break;
            }
        }
        if (isNormalString)
        {
            TempQuery.strQuery_ = it->getQuery();
            TempQuery.freq_ = it->getCount();
            TempQuery.HitNum_ = it->getHitDocsNum();
            querylist.push_back(TempQuery );
        }
    }
    LoadItem();
    buildItemVector();
    LOG(INFO) << " buildDbIndex for query size : " << querylist.size() << ", time_string: " << time_string;
    buildDbIndex(querylist);
    isUpdating_Wat_ = true;
    wa_.Clear();
    LOG(INFO) << " buildDbIndex for query finished";
    buildWat_array(false);
}

void AutoFillChildManager::flush()
{
    dbTable_.flush();
    dbItem_.flush();
    SaveItem();
    SaveWat();
    if (idManager_) idManager_->flush();
}

bool AutoFillChildManager::buildIndex(const std::list<ItemValueType>& queryList)
{
    QueryType Query;
    std::list<QueryType> queryLists;
    std::list<ItemValueType>::const_iterator it;
    uint32_t count = 0;
    for(it = queryList.begin(); it != queryList.end(); it++)
    {
        count++;
        std::string str;
        (*it).get<2>().convertString(str, izenelib::util::UString::UTF_8);
        Query.strQuery_ = str;
        Query.freq_ = (FREQ_TYPE)(*it).get<0>();
        Query.HitNum_ = (*it).get<1>();
        queryLists.push_back(Query);
    }
    buildDbIndex(queryLists);
    isUpdating_Wat_ = true;
    buildWat_array(false);
    isUpdating_Wat_ = false;
    return true;
}
};
