/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cerrno>
#include <cstdio>

#include <event2/buffer.h>

#include "transmission.h"
#include "file.h"
#include "log.h"
#include "platform.h" /* tr_lock */
#include "tr-assert.h"
#include "utils.h"

tr_log_level __tr_message_level = TR_LOG_ERROR;

static bool myQueueEnabled = false;
static tr_log_message* myQueue = nullptr;
static tr_log_message** myQueueTail = &myQueue;
static int myQueueLength = 0;

#ifndef _WIN32

/* make null versions of these win32 functions */
static inline bool IsDebuggerPresent(void)
{
    return false;
}

#endif

/***
****
***/

tr_log_level tr_logGetLevel(void)
{
    return __tr_message_level;
}

/***
****
***/

static tr_lock* getMessageLock(void)
{
    static tr_lock* l = nullptr;

    if (l == nullptr)
    {
        l = tr_lockNew();
    }

    return l;
}

tr_sys_file_t tr_logGetFile(void)
{
    static bool initialized = false;
    static tr_sys_file_t file = TR_BAD_SYS_FILE;

    if (!initialized)
    {
        int const fd = tr_env_get_int("TR_DEBUG_FD", 0);

        switch (fd)
        {
        case 1:
            file = tr_sys_file_get_std(TR_STD_SYS_FILE_OUT, nullptr);
            break;

        case 2:
            file = tr_sys_file_get_std(TR_STD_SYS_FILE_ERR, nullptr);
            break;
        }

        initialized = true;
    }

    return file;
}

void tr_logSetLevel(tr_log_level level)
{
    __tr_message_level = level;
}

void tr_logSetQueueEnabled(bool isEnabled)
{
    myQueueEnabled = isEnabled;
}

bool tr_logGetQueueEnabled(void)
{
    return myQueueEnabled;
}

tr_log_message* tr_logGetQueue(void)
{
    tr_lockLock(getMessageLock());

    auto* const ret = myQueue;
    myQueue = nullptr;
    myQueueTail = &myQueue;
    myQueueLength = 0;

    tr_lockUnlock(getMessageLock());
    return ret;
}

void tr_logFreeQueue(tr_log_message* list)
{
    while (list != nullptr)
    {
        tr_log_message* next = list->next;
        tr_free(list->message);
        tr_free(list->name);
        tr_free(list);
        list = next;
    }
}

/**
***
**/

char* tr_logGetTimeStr(char* buf, size_t buflen)
{
    struct timeval tv;
    tr_gettimeofday(&tv);
    time_t const seconds = tv.tv_sec;
    int const milliseconds = (int)(tv.tv_usec / 1000);
    char msec_str[8];
    tr_snprintf(msec_str, sizeof msec_str, "%03d", milliseconds);

    struct tm now_tm;
    tr_localtime_r(&seconds, &now_tm);
    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", &now_tm);

    tr_snprintf(buf, buflen, "%s.%s", date_str, msec_str);
    return buf;
}

bool tr_logGetDeepEnabled(void)
{
    static int8_t deepLoggingIsActive = -1;

    if (deepLoggingIsActive < 0)
    {
        deepLoggingIsActive = (int8_t)(IsDebuggerPresent() || tr_logGetFile() != TR_BAD_SYS_FILE);
    }

    return deepLoggingIsActive != 0;
}

void tr_logAddDeep(char const* file, int line, char const* name, char const* fmt, ...)
{
    tr_sys_file_t const fp = tr_logGetFile();

    if (fp != TR_BAD_SYS_FILE || IsDebuggerPresent())
    {
        struct evbuffer* buf = evbuffer_new();
        char* base = tr_sys_path_basename(file, nullptr);

        char timestr[64];
        evbuffer_add_printf(buf, "[%s] ", tr_logGetTimeStr(timestr, sizeof(timestr)));

        if (name != nullptr)
        {
            evbuffer_add_printf(buf, "%s ", name);
        }

        va_list args;
        va_start(args, fmt);
        evbuffer_add_vprintf(buf, fmt, args);
        va_end(args);
        evbuffer_add_printf(buf, " (%s:%d)" TR_NATIVE_EOL_STR, base, line);

        size_t message_len = 0;
        char* const message = evbuffer_free_to_str(buf, &message_len);

#ifdef _WIN32
        OutputDebugStringA(message);
#endif

        if (fp != TR_BAD_SYS_FILE)
        {
            tr_sys_file_write(fp, message, message_len, nullptr, nullptr);
        }

        tr_free(message);
        tr_free(base);
    }
}

/***
****
***/

void tr_logAddMessage(char const* file, int line, tr_log_level level, char const* name, char const* fmt, ...)
{
    int const err = errno; /* message logging shouldn't affect errno */
    char buf[1024];
    va_list ap;
    tr_lockLock(getMessageLock());

    /* build the text message */
    *buf = '\0';
    va_start(ap, fmt);
    int const buf_len = evutil_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (buf_len < 0)
    {
        goto FINISH;
    }

#ifdef _WIN32

    if ((size_t)buf_len < sizeof(buf) - 3)
    {
        buf[buf_len + 0] = '\r';
        buf[buf_len + 1] = '\n';
        buf[buf_len + 2] = '\0';
        OutputDebugStringA(buf);
        buf[buf_len + 0] = '\0';
    }
    else
    {
        OutputDebugStringA(buf);
    }

#endif

    if (!tr_str_is_empty(buf))
    {
        if (tr_logGetQueueEnabled())
        {
            auto* const newmsg = tr_new0(tr_log_message, 1);
            newmsg->level = level;
            newmsg->when = tr_time();
            newmsg->message = tr_strndup(buf, buf_len);
            newmsg->file = file;
            newmsg->line = line;
            newmsg->name = tr_strdup(name);

            *myQueueTail = newmsg;
            myQueueTail = &newmsg->next;
            ++myQueueLength;

            if (myQueueLength > TR_LOG_MAX_QUEUE_LENGTH)
            {
                tr_log_message* old = myQueue;
                myQueue = old->next;
                old->next = nullptr;
                tr_logFreeQueue(old);
                --myQueueLength;
                TR_ASSERT(myQueueLength == TR_LOG_MAX_QUEUE_LENGTH);
            }
        }
        else
        {
            char timestr[64];

            tr_sys_file_t fp = tr_logGetFile();

            if (fp == TR_BAD_SYS_FILE)
            {
                fp = tr_sys_file_get_std(TR_STD_SYS_FILE_ERR, nullptr);
            }

            tr_logGetTimeStr(timestr, sizeof(timestr));

            if (name != nullptr)
            {
                tr_sys_file_write_fmt(fp, "[%s] %s: %s" TR_NATIVE_EOL_STR, nullptr, timestr, name, buf);
            }
            else
            {
                tr_sys_file_write_fmt(fp, "[%s] %s" TR_NATIVE_EOL_STR, nullptr, timestr, buf);
            }

            tr_sys_file_flush(fp, nullptr);
        }
    }

FINISH:
    tr_lockUnlock(getMessageLock());
    errno = err;
}
