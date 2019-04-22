#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <vector>

namespace LinearCheckerLogger {

class LClog {
public:
    enum LogType {
        INVOKE_LOG,
        RESPONSE_LOG
    };
    
    LClog(std::string const & threadId, unsigned long long timeStamp, LogType type)
        : threadId_(threadId), timeStamp_(timeStamp), type_(type) {}
    virtual ~LClog() {}

    virtual std::string const & getLogContent() const = 0;

    std::string const & getThreadId() const {
        return threadId_;
    }

    unsigned long long getTimeStamp() const {
        return timeStamp_;
    }

    LogType getType() const {
        return type_;
    }

protected:
    std::string threadId_;
    unsigned long long timeStamp_;
    LogType type_;
    
};

class FetchStoreLog : public LClog {
public:
    FetchStoreLog(std::string const & threadId, unsigned long long timeStamp, LogType type)
        : LClog(threadId, timeStamp, type) {}

    std::string const & getLogContent() const {
        return content_;
    }

    void addOneAddrData(std::string const & addr, std::string const & data) {
        addrVec_.push_back(addr);
        dataVec_.push_back(data);
    }

    void genContent();


private:
    std::string content_;
    std::vector<std::string> addrVec_;
    std::vector<std::string> dataVec_;

};







class LogWriter {
public:
    
    LogWriter(std::string fileName, bool isNew);
    ~LogWriter();

    void writeLogsSync(std::vector<LClog> const & logs);

    void writeLogs(std::vector<LClog> const & logs);

    void writeLog(LClog const & log);
    
    void writeLogSync(LClog const & log);


private:
    void doWriteLog(LClog const & log);


    std::ofstream log_fs_;
    const std::string seperator_;
    std::mutex mtx_;

};

















}

