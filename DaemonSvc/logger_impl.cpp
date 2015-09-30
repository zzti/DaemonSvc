#include <Windows.h>
#include <boost/thread/lock_guard.hpp>
#include "any_lexical_cast.h"
#include "self_path.h"
#include "logger_impl.h"


bool CLoggerImpl::init(const std::string& dir, const unsigned long max_size)
{
    bool ret = false;

    do 
    {
        if (!CSelfPath::get_instance_ref().valid())
        {
            printf_s("can not get self path\r\n");
            break;
        }

        std::string log_dir = dir;
        if (log_dir.empty())
        {
            log_dir = CSelfPath::get_instance_ref().get_dir() + "\\log";
            //file_path size limit 248, see MSDN CreateDirectory
            if (!CreateDirectoryA(log_dir.c_str(), NULL))
            {
                CLastError e;
                if (ERROR_ALREADY_EXISTS != e.code())
                {
                    print_last_err(e, "CreateDirectory for create log dir[%s] fail", log_dir.c_str());
                    break;
                }
            }
        }

        {
            boost::lock_guard<boost::mutex> locker(m_log_file_lock);

            m_log_file_dir = log_dir;
            m_log_file_name = CSelfPath::get_instance_ref().get_name();
            m_log_file_max_size = max_size;
            if (0 == m_log_file_max_size)
            {
                m_log_file_max_size = 10 * 1024 * 1024;//10MB
            }

            m_log_file_handle.reset(new_log_file(m_log_file_dir, m_log_file_name));
            m_write_len = 0;
            ret = m_log_file_handle.valid();
        }

    } while (false);

    return ret;
}

bool CLoggerImpl::log_string(const LOG_LEVEL level, const char* file, const int line, const std::string& s)
{
    std::string s_to_write = build_prefix(level, file, line);
    s_to_write += s + "\r\n";
    return write(level, s_to_write);
}

bool CLoggerImpl::log_bytes(const LOG_LEVEL level, const char* file, const int line, const void *buf, const unsigned long len, const std::string& prefix)
{
    const std::string len_str = string_lexical_cast<char>(len);
    std::string s_to_write = build_prefix(level, file, line)
        + prefix + "\r\n" + "@@@@@begin, buffer size = " + len_str + "@@@@@\r\n";
    s_to_write.append(reinterpret_cast<const char *>(buf), len);
    s_to_write += "\r\n@@@@@end, buffer size = " + len_str + "@@@@@\r\n";
    return write(level, s_to_write);
}

bool CLoggerImpl::log_last_error(const LOG_LEVEL level, const char* file, const int line, CLastError& e, const std::string& prefix)
{
    std::string s_to_write = build_prefix(level, file, line) + prefix;
    s_to_write += ", error code: " + string_lexical_cast<char>(e.code()) + ", error msg: " + e.str() + "\r\n";
    return write(level, s_to_write);
}

std::string CLoggerImpl::build_prefix(const LOG_LEVEL level, const char* file, const int line)
{
    std::string s;

    SYSTEMTIME systime = {0};
    GetLocalTime(&systime);

    const size_t time_buf_size = 100;
    char time_buf[time_buf_size] = {0};
    sprintf_s(time_buf, "%04d/%02d/%02d-%02d:%02d:%02d.%03d ",
        systime.wYear, systime.wMonth, systime.wDay,
        systime.wHour, systime.wMinute, systime.wSecond,
        systime.wMilliseconds);
    s += time_buf;//buf is large enough to hold string and a null-terminated ch

    {
        static const DWORD pid = GetCurrentProcessId();
        const DWORD tid = GetCurrentThreadId();//todo: tls
        s += "[" + string_lexical_cast<char>(pid) + ":" + string_lexical_cast<char>(tid) + "] ";
    }

    switch (level)
    {
    case LOG_DEBUG:
        s += "DEBUG ";
        break;

    case LOG_INFO:
        s += "INFO  ";
        break;

    case LOG_ERROR:
        s += "ERROR ";
        break;

    default:
        s += "????? ";
        break;
    }

    s += "[";
    s += file;
    s += ":" + string_lexical_cast<char>(line) + "] ";

    return s;
}

HANDLE CLoggerImpl::new_log_file(const std::string& log_file_dir, const std::string& log_file_name)
{
    HANDLE h = INVALID_HANDLE_VALUE;

    do 
    {
        std::string file_path = log_file_dir + "\\" + log_file_name;

        //get current time string
        {
            SYSTEMTIME systime = {0};
            GetLocalTime(&systime);

            const size_t time_buf_size = 100;
            char time_buf[time_buf_size] = {0};
            //have a "." before after
            sprintf_s(time_buf, ".%04d%02d%02d%02d%02d%02d.",
                systime.wYear, systime.wMonth, systime.wDay,
                systime.wHour, systime.wMinute, systime.wSecond);
            file_path += time_buf;//buf is large enough to hold string and a null-terminated ch
        }

        {
            static const DWORD pid = GetCurrentProcessId();
            file_path += string_lexical_cast<char>(pid);
        }

        file_path += ".log";
        h = CreateFileA(file_path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        CLastError e;//right behind Windows API call
        if (INVALID_HANDLE_VALUE == h)
        {
            print_last_err(e, "CreateFile fail, file path: %s", file_path.c_str());
            break;
        }

        printf_s("created new log file: %s\r\n", file_path.c_str());

    } while (false);

    return h;
}

bool CLoggerImpl::write(const LOG_LEVEL level, const std::string& s)
{
    //todo: level
    printf_s("%s", s.c_str());
    OutputDebugStringA(s.c_str());

    bool ret = false;

    {
        boost::lock_guard<boost::mutex> locker(m_log_file_lock);

        do 
        {
            if (!m_log_file_handle.valid())
            {
                printf_s("log file handle is invalid\r\n");
                break;
            }

            DWORD written_bytes = 0;
            if (!WriteFile(m_log_file_handle.get(), s.c_str(), s.size(), &written_bytes, NULL))
            {
                print_last_err(CLastError(), "WriteFile fail");
                break;
            }

            if (written_bytes != s.size())
            {
                printf_s("not all written, to write: %lu, written: %lu\r\n", s.size(), written_bytes);
            }
            m_write_len += written_bytes;

            if (m_write_len >= m_log_file_max_size)
            {
                HANDLE h = new_log_file(m_log_file_dir, m_log_file_name);
                if (INVALID_HANDLE_VALUE == h)
                {
                    printf_s("new log file fail\r\n");
                }
                else
                {
                    m_log_file_handle.reset(h);
                    m_write_len = 0;
                }
            }
            ret = true;

        } while (false);
    }

    return ret;
}






