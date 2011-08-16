#ifndef PROCESS_CONTROLLERS_DOCUMENTS_CONTROLLER_H
#define PROCESS_CONTROLLERS_DOCUMENTS_CONTROLLER_H
/**
 * @file process/controllers/DocumentsController.h
 * @author Ian Yang
 * @date Created <2010-06-02 09:58:52>
 */
#include "Sf1Controller.h"

#include <string>

namespace sf1r
{

/// @addtogroup controllers
/// @{

/**
 * @brief Controller \b documents
 *
 * Create, update, destroy or fetch documents.
 */
class DocumentsController : public Sf1Controller
{
    /// @brief default count in page.
    static const std::size_t kDefaultPageCount;

public:
    /// @brief alias for search
    void index()
    {
        search();
    }

    void search();
    void get();
    void similar_to();
    void duplicate_with();
    void similar_to_image();
    void create();
    void update();
    void destroy();
    void get_topic();
    void get_topic_with_sim();
    void get_freq_group_labels();
    void set_top_group_label();
    void visit();

private:
    bool requireDOCID();
    bool setLimit();
    bool parseCollection();

    std::string collection_;
};

/// @}

} // namespace sf1r

#endif // PROCESS_CONTROLLERS_DOCUMENTS_CONTROLLER_H
