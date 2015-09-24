#include <cassert>
#include <Windows.h>
#include <boost/bind.hpp>
#include "logger.h"
#include "time_point_task.h"


CTimePointTask::CTimePointTask(const TaskFunc& f, const PeriodTime& period, const DWORD deviation_minutes)
    : m_started(false)
    , m_f(f)
    , m_period(period)
    , m_deviation_minutes(deviation_minutes)
    , m_hExitEvent(NULL)
{
    memset(&m_last_execute_time, 0, sizeof(m_last_execute_time));
}

CTimePointTask::~CTimePointTask(void)
{
    if (m_started)
    {
        stop();
    }
}

bool CTimePointTask::is_started() const
{
    return m_started;
}

bool CTimePointTask::start()
{
    if (m_started)
    {
        return true;
    }
    else
    {
        assert(NULL == m_hExitEvent);

        m_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (NULL == m_hExitEvent)
        {
            ErrorLogLastErr(CLastError(), TSTR("CreateEvent for notify time point task thread exit fail"));
        }
        else
        {
            try
            {
                m_worker_thread = boost::thread(boost::bind(&CTimePointTask::worker_func, this));
                m_started = true;
            }
            catch (boost::thread_resource_error& e)
            {
                ErrorLogA("create time point task worker thread fail, error: %s", e.what());
            }
        }

        return m_started;
    }
}

void CTimePointTask::stop()
{
    if (m_started)
    {
        assert(m_hExitEvent);

        SetEvent(m_hExitEvent);
        if (m_worker_thread.joinable())
        {
            m_worker_thread.join();
        }
        CloseHandle(m_hExitEvent);
        m_hExitEvent = NULL;

        m_started = false;
    }
}

void CTimePointTask::worker_func()
{
    InfoLogA("time point task worker thread func begin");
    ErrorLogA("time point task not implemented");

    while (true)
    {
        SYSTEMTIME systime = {0};
        GetLocalTime(&systime);

        //������һ��Ӧִ�е�ʱ��
        //����ǰʱ�����һ��Ӧִ��ʱ��֮����û�б�ִ�й������ϴ�ִ��ʱ���Ƿ�����һ��Ӧִ��ʱ��֮��
        //���У��������һ�εȴ�
        //��û�У����ж��Ƿ�����Χ�ڣ�����ǰʱ��-��һ��Ӧִ��ʱ�� �Ƿ� <= ���
        //���ڣ���ִ�У���������һ��ִ��ʱ��
        //�����ڣ��������һ�εȴ�

        //todo
        const DWORD wait_result = WaitForSingleObject(m_hExitEvent, INFINITE);
        if (WAIT_OBJECT_0 == wait_result)
        {
            InfoLogA("got exit notify");
            break;
        }
    }

    InfoLogA("time point task worker thread func end");
}
