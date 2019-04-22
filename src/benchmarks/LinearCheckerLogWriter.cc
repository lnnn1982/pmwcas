#include "LinearCheckerLogWriter.h"

namespace LinearCheckerLogger {


void FetchStoreLog::genContent() {
    const std::string seperator = " ";
    std::ostringstream  oss;
    for (auto const & addr : addrVec_) {
        oss << addr << seperator;
    }

    for(unsigned long i = 0; i < dataVec_.size(); i++) {
        oss << dataVec_[i];
        if(i != dataVec_.size()-1) {
            oss << seperator;
        }
    }

    content_ = oss.str();
}

LogWriter::LogWriter(std::string fileName, bool isNew) : seperator_(" ") {
    if(isNew) {
        log_fs_.open (fileName.c_str(), std::ofstream::out | std::ofstream::trunc);
    }
    else {
        log_fs_.open (fileName.c_str(), std::ofstream::out | std::ofstream::app);
    }
}

LogWriter::~LogWriter() {
    log_fs_.close();
}

void LogWriter::writeLogsSync(std::vector<LClog> const & logs) {
    std::lock_guard<std::mutex> lck(mtx_);
    writeLogs(logs);
}

void LogWriter::writeLogs(std::vector<LClog> const & logs) {
    for (auto const & log : logs) {
        doWriteLog(log);
    }

    log_fs_.flush();
}

void LogWriter::writeLog(LClog const & log) {
    doWriteLog(log);
    log_fs_.flush();
}

void LogWriter::writeLogSync(LClog const & log) {
    std::lock_guard<std::mutex> lck(mtx_);
    writeLog(log);
}

void LogWriter::doWriteLog(LClog const & log) {
    log_fs_ << log.getTimeStamp() << seperator_;
    if(log.getType() == LClog::INVOKE_LOG) {
        log_fs_ << "I" << seperator_;
    }
    else if(log.getType() == LClog::RESPONSE_LOG) {
        log_fs_ << "R" << seperator_;
    }

    if(!log.getThreadId().empty()) {
        log_fs_ << log.getThreadId() << seperator_;
    }

    log_fs_ << log.getLogContent() << "\n";
}


}
