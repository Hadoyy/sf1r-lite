#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <document-manager/DocumentManager.h>
#include <document-manager/DocContainer.h>
#include <document-manager/Document.h>

#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include <3rdparty/am/luxio/array.h>
#include <util/bzip.h>
#include <ir/index_manager/utility/BitVector.h>
#include <boost/filesystem.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/archive_exception.hpp>
#include <boost/scoped_array.hpp>
#include <boost/lexical_cast.hpp>
#include <util/izene_serialization.h>
#include <compression/compressor.h>

using namespace std;
namespace bf = boost::filesystem;

int main(int argc, char * argv[])
{
    using namespace sf1r;
    try
    {
        if (argc < 2)
        {
            printf("please input the collection data base path!\n");
            return -1;
        }
        std::string col_data_base = argv[1];
        std::cout << "begin migrate the collection data : " << col_data_base << std::endl;
        std::string docdata_path = col_data_base + "/default-collection-dir/dm/";
        std::string docdata_newpath = col_data_base + "/default-collection-dir/dm_dump";
        {
            DocContainer tmpDocMgr(docdata_path);

            if (!tmpDocMgr.open())
            {
                std::cout << "open old data failed." << std::endl;
                return -1;
            }
            ofstream ofs(docdata_newpath.c_str());
            Document doc;
            for (size_t i = 0; i <= tmpDocMgr.getMaxDocId(); ++i)
            {
                if( tmpDocMgr.get(i, doc) )
                {
                    ofs << "id: " << i << std::endl;
                    Document::property_const_iterator it = doc.propertyBegin();
                    for(; it != doc.propertyEnd(); ++it)
                    {
                        try
                        {
                            izenelib::util::UString propU = doc.property(it->first).get<izenelib::util::UString>();
                            std::string propstr;
                            propU.convertString(propstr, izenelib::util::UString::UTF_8);
                            ofs << it->first << "==" << propstr << std::endl;
                        }
                        catch(const boost::bad_get& e)
                        {
                            std::cerr << "get doc property failed : " << e.what();
                        }
                    }
                }
                if (i%10000==0)
                {
                    std::cout << " converted doc : " << i << std::endl;
                }
            }
        }

        std::cout << "convert finished." << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exit because of exception: "
                  << e.what() << std::endl;
    }

    return 1;
}
