/* A service wrapper for Redis R workers
 * Copyright (C) 2010 by Bryan Lewis
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 */

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <windows.h>
#include <process.h>
#include <Tlhelp32.h>

#ifndef MAX_PATH
#define MAX_PATH 8192
#endif

#define WMAX 32768
#define MAX_WORKERS 16

char *NTServiceName = "doRedis";  // The Windows Service name
int np = MAX_WORKERS;             // Number of workers (may be set in main)
char WORKER[WMAX];

typedef struct
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
} WORKERINFO;

WORKERINFO workers[MAX_WORKERS];

SERVICE_STATUS NTServiceStatus;
SERVICE_STATUS_HANDLE NTServiceStatusHandle;
HANDLE NTServiceThreadHandle = NULL;
HANDLE ServiceControlEvent = 0;

// Start a worker in the indicated slot
int
startWorker (int slot)
{
  DWORD dwCreationflags;
  BOOL bSuccess = FALSE;

  if(slot>=np) return -1;
  ZeroMemory (&(workers[slot].si), sizeof (STARTUPINFO));
  workers[slot].si.cb = sizeof (STARTUPINFO);
  ZeroMemory (&(workers[slot]).pi, sizeof (PROCESS_INFORMATION));
  dwCreationflags = GetPriorityClass (GetCurrentProcess ());
// Create the child R worker processes
  bSuccess = CreateProcess (NULL, WORKER,
                            NULL,       // process security attributes 
                            NULL,       // primary thread security attributes 
                            TRUE,       // handles are inherited 
                            dwCreationflags,
                            NULL,       // use parent's environment 
                            NULL,       // startup path
                            &(workers[slot].si),
                            &(workers[slot].pi));
  return (int) bSuccess;
}

void
stopService ()
{
  DWORD iProcessID =  GetCurrentProcessId();
  PROCESSENTRY32 pe;
  ZeroMemory (&pe, sizeof (PROCESSENTRY32));
  pe.dwSize = sizeof (PROCESSENTRY32);
  HANDLE hSnap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
  if (Process32First (hSnap, &pe))
    {
      BOOL bContinue = TRUE;
      while (bContinue)
        {
          // only kill child processes 
          if (pe.th32ParentProcessID == iProcessID)
            {
              HANDLE hChildProc =
                OpenProcess (PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID);
              if (hChildProc)
                {
                  TerminateProcess (hChildProc, 1);
                  CloseHandle (hChildProc);
                }
            }
          bContinue = Process32Next (hSnap, &pe);
        }
    }
}


void
scReportStatus (DWORD curState, DWORD exitCode, DWORD hint)
{
  static DWORD checkPoint = 1;
  if (curState == SERVICE_START_PENDING)
    NTServiceStatus.dwControlsAccepted = 0;
  else
    NTServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  NTServiceStatus.dwCurrentState = curState;
  NTServiceStatus.dwWin32ExitCode = exitCode;
  NTServiceStatus.dwWaitHint = hint;
  if ((curState == SERVICE_RUNNING) || (curState == SERVICE_STOPPED))
    NTServiceStatus.dwCheckPoint = 0;
  else
    NTServiceStatus.dwCheckPoint = checkPoint++;
  SetServiceStatus (NTServiceStatusHandle, &NTServiceStatus);
}

void WINAPI
serviceCtrl (DWORD code)
{
  switch (code)
    {
    case SERVICE_CONTROL_STOP:
      scReportStatus (SERVICE_STOP_PENDING, NO_ERROR, 0);
      SetEvent(ServiceControlEvent);
      break;
    default:
      break;
    }
}

void WINAPI
serviceMain (DWORD argc, LPTSTR * argv)
{
  int j;
  DWORD w;
  NTServiceStatusHandle
    = RegisterServiceCtrlHandler (NTServiceName, serviceCtrl);
  if (!NTServiceStatusHandle)
    {
      return;
    }
  NTServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  NTServiceStatus.dwServiceSpecificExitCode = 0;
  scReportStatus (SERVICE_START_PENDING, NO_ERROR, 3000);
  ServiceControlEvent = CreateEvent(0, FALSE, FALSE, 0);
  scReportStatus (SERVICE_RUNNING, NO_ERROR, 0);
  for(j=0;j<np;++j){
    startWorker(j);
  }
  while (NTServiceStatus.dwCurrentState == SERVICE_RUNNING)
    {
      for(j=0;j<np;++j)
        {
          w = WaitForSingleObject (workers[j].pi.hProcess, 0);
          if(w == 0) {
// Worker j has exited, restart it.
            startWorker(j);
          }
        }
      Sleep(500);
    }
  stopService();
  scReportStatus (SERVICE_STOPPED, NO_ERROR, 0);
  CloseHandle(ServiceControlEvent);
  ServiceControlEvent = 0;
}

int
main (int argc, char **argv)
{
  FILE *fp;
  char config[MAX_PATH];
  char *fpbuf;
  char *line = NULL;

  char *rterm;
  char *redisHost;
  char *redisPort;
  char *redisJobQueue;
  char *restart;

  memset(config, 0, MAX_PATH);
  strncpy(config,dirname(argv[0]),MAX_PATH);
  strncat(config, "\\doRedis.ini", MAX_PATH);

// PROCESS THE ENVIRONMENT VARIABLE SETTINGS IN doRedis.ini:
  fp = fopen(config, "r");
  if (!fp) exit(-1);
  fpbuf = (char *)malloc(32768);
  fread (fpbuf, 1, 32768, fp);
  fclose(fp);

  line = strtok(fpbuf, "\n");
  while(line) {
    putenv(line);
    line = strtok(NULL, "\n");
  }
  if (fpbuf) free(fpbuf);

  rterm = getenv("Rterm");
  np    = atoi(getenv("Np"));
  if(!np) np = 1;
  redisHost = getenv("RedisHost");
  redisPort = getenv("RedisPort");
  redisJobQueue = getenv("RedisJobQueue");
  restart = getenv("RedisRestart");
  if(!redisHost) redisHost = strdup("localhost");
  if(!redisPort) redisPort = strdup("6379");
  if(!redisJobQueue) redisJobQueue = strdup("RSF");
  if(!restart) restart = strdup("10");

  memset(WORKER,0,WMAX);
  strncpy(WORKER,rterm,WMAX);
  strncat(WORKER," --slave -e \"require(doRedis);redisWorker(queue='",WMAX);
  strncat(WORKER,redisJobQueue,WMAX);
  strncat(WORKER,"', host='", WMAX);
  strncat(WORKER,redisHost,WMAX);
  strncat(WORKER,"', port=",WMAX);
  strncat(WORKER,redisPort,WMAX);
  strncat(WORKER,", iter=",WMAX);
  strncat(WORKER,restart,WMAX);
  strncat(WORKER, ")\"", WMAX);

//DEBUG
/*
printf("Rterm = %s\n",rterm);
printf("np = %d\n\n",np);
printf("%s\n",WORKER);
return 0;
*/

  if(restart) free(restart);
  if(redisJobQueue) free(redisJobQueue);
  if(redisPort) free(redisPort);
  if(redisHost) free(redisHost);

  SERVICE_TABLE_ENTRY dispatchTable[] = {
    {NTServiceName, (LPSERVICE_MAIN_FUNCTION) serviceMain},
    {NULL, NULL}
  };
  StartServiceCtrlDispatcher (dispatchTable);
  return 0;
}
