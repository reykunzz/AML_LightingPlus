#ifndef _FLOGGER_H
#define _FLOGGER_H

#include <stdio.h>
#include <stdarg.h>

class FLogger
{
public:
    FLogger(const char* szFileName, bool bClearFile = true);
    ~FLogger();

    void Info(const char* szMessage, ...);
    void Error(const char* szMessage, ...);
    void Write(const char* szMessage, ...);

private:
    bool Open();
    bool OpenViaMediaStore();
    void WriteLine(const char* szPrefix, const char* szMessage, va_list args);

    FILE* m_pFile;
    char m_szFileName[128];
    bool m_bClearOnOpen;
    bool m_bGaveUp;   // jalur MediaStore hanya dicoba sekali (hindari JNI + entri duplikat tiap baris log)
};

#endif // _FLOGGER_H