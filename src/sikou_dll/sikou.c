/*
 * sikou.c
 * Copyright (C) 2004 - 2010 Kunihito Hoki
 * Version 4.1.3 - Jan 1 2010
 */

#include <windows.h>

#pragma warning(disable:4201)
#include <commctrl.h>
#pragma warning(default:4201)

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <strsafe.h>
#include "sikou.h"

#define CsaCallConv           __cdecl
#define EventNotThinking      0
#define EventDlgUp            1
#define NumEvents             2

#define SizePipeReadBuf       4096
#define SizePipeLineBuf       256
#define SizeMainDlgStatus     256
#define SizeInfoBuf           256
#define SizeTempBuf           256
#define SizeTimeSpentBuf      256
#define SizeBoard             81
#define SizeComMove           8
#define DefaultTCM            0
#define DefaultTC             3
#define MinResign             200
#define DefaultResign         1500
#define StatusShowResult      ( 1U << 0 )
#define StatusThreadTimeOK    ( 1U << 1 )
#define StatusAudio           ( 1U << 2 )
#define StatusPonder          ( 1U << 3 )
#define StatusNarrowBook      ( 1U << 4 )
#define StatusStrictTime      ( 1U << 5 )
#define StatusThinking        ( 1U << 6 )
#define StatusPondering       ( 1U << 7 )
#define StatusReady           ( 1U << 8 )
#define WmNewBoard            ( WM_APP +  0 )
#define WmChangeMainDlgStatus ( WM_APP +  1 )
#define WmChangeInfo          ( WM_APP +  2 )
#define WmChangeStat          ( WM_APP +  3 )
#define WmGameStart           ( WM_APP +  4 )
#define WmGameEnd             ( WM_APP +  5 )
#define WmThinkStart          ( WM_APP +  6 )
#define WmThinkEnd            ( WM_APP +  7 )
#define WmPonderStart         ( WM_APP +  8 )
#define WmPonderEnd           ( WM_APP +  9 )
#define WmRetractBoard        ( WM_APP + 10 )
#define WmMoveBoard           ( WM_APP + 11 )
#define WmRead                ( WM_APP + 12 )
#define WmPonderOn            ( WM_APP + 13 )
#define WmPonderOff           ( WM_APP + 14 )
#define WmChangeTimeSpent     ( WM_APP + 15 )
#define WmTimeRemain          ( WM_APP + 16 )
#define SBPartStatus          0
#define SBPartMessage         1
#define MemUsedBase           230
#define HashSizeMin           24
#define HashBase              19
#define ScoreMateL            30001
#define ScoreMateU            32598
#define ScoreRep              32599
#define ScoreUnavailable      32600

#define Empty                 0
#define Pawn                  1
#define Lance                 2
#define Knight                3
#define Silver                4
#define Gold                  5
#define Bishop                6
#define Rook                  7
#define King                  8
#define ProPawn               9
#define ProLance             10
#define ProKnight            11
#define ProSilver            12
#define Horse                14
#define Dragon               15
#define Promote               8

#define APIError(msg) FuncAPIError( __LINE__, TEXT(msg) )
#define Error(msg)    FuncError( __LINE__, TEXT(msg) )
#define ErrorA(msg)   FuncErrorA( __LINE__, msg )

static LRESULT CALLBACK HStaticProc( HWND hWnd , UINT uMsg,
				     WPARAM wParam, LPARAM lParam );
static LRESULT CALLBACK HButtonProc( HWND hWnd , UINT uMsg,
				     WPARAM wParam, LPARAM lParam );
static DWORD GetTotalPhys( VOID );
static VOID WritePipe( LPCSTR lpszFormat, ... );
static UINT GetTime( VOID );
static BOOL ReadLine( HANDLE hChildStdoutRd );
static VOID CreateProcessAndPipe( LPHANDLE lphChildStdinRd,
				  LPHANDLE lphChildStdinWr,
				  LPHANDLE lphChildStdoutRd,
				  LPHANDLE lphChildStdoutWr );
static VOID FuncAPIError( DWORD dwLine, LPTSTR pszMsg );
static VOID FuncError( DWORD dwLine, LPTSTR pszMsg );
static VOID FuncErrorA( DWORD dwLine, LPSTR pszMsg );
static LRESULT CALLBACK MainDlgProc( HWND hWnd , UINT uMsg, WPARAM wParam,
				     LPARAM lParam );
static DWORD WINAPI ThreadProcMainDlg( LPVOID lpParameter );
static DWORD WINAPI ThreadProcPipeRead( LPVOID lpParameter );

static WNDPROC wpOrigStaticProc;
static WNDPROC wpOrigButtonProc;
static DWORD dwStatus;
static UCHAR Board[SizeBoard];
static UCHAR HistorySave[1001][2];
static CHAR szMainDlgStatus[SizeMainDlgStatus];
static CHAR szPipeReadBuf[SizePipeReadBuf];
static CHAR szPipeLineBuf[SizePipeLineBuf];
static CHAR szComMove[SizeComMove];
static CHAR szInfoBuf[SizeInfoBuf];
static CHAR szTimeSpentBuf[SizeTimeSpentBuf];
static BYTE abTempBuffer[SizeTempBuf];
static HANDLE hEvents[NumEvents];
static UCHAR BoardInitial[SizeBoard];
static HANDLE hChildStdinWr;
static HINSTANCE hinstDLL;
static HWND hwndMainDlg;
static UINT uiTimeStart;
static INT iScore;
static INT iNPS;
static INT iCPU;
static INT iMem;
static INT iTCM;
static INT iTC;
static INT iMovesSave;
static const volatile int *chudan_flag;

static LPCSTR lpszNew            = "new\n";
static LPCSTR lpszTLPNumD        = "tlp num %d\n";
static LPCSTR lpszReadD          = "read . t %d\n";
static LPCSTR lpszReadS          = "read %s\n";
static LPCSTR lpszHashD          = "hash %d\n";
static LPCSTR lpszResignD        = "resign %d\n";
static LPCSTR lpszLimitTimeDD    = "limit time %d %d\n";
static LPCSTR lpszLimitTimeS     = "limit time %s\n";
static LPCSTR lpszTimeRemainDD   = "time remain %d %d\n";
static LPCSTR lpszPonderOn       = "ponder on\n";
static LPCSTR lpszPonderOff      = "ponder off\n";
static LPCSTR lpszBookNarrow     = "book narrow\n";
static LPCSTR lpszBookWide       = "book wide\n";
static LPCSTR lpszMove           = "move\n";
static LPCSTR lpszResign         = "resign";
static LPCSTR lpszMoveNow        = "s\n";
static LPCSTR lpszMoveBoard      = "move %d%d%d%d%s\n";
static LPCTSTR lpszBoardDat      = TEXT( "board.dat" );
static LPCTSTR lpszChildCmd      = TEXT( "bonanza.exe csa_shogi" );
static LPCTSTR lpszBonanza       = TEXT( "Bonanza" );
static LPCTSTR lpszReady         = TEXT( "Ready" );
static LPCTSTR lpszIdling        = TEXT( "Idling" );
static LPCTSTR lpszPondering     = TEXT( "Pondering" );
static LPCTSTR lpszThinking      = TEXT( "Thinking" );

static LPCTSTR lpszAbout
= TEXT( "About Bonanza: Copyright (C) 2004 - 2011 Kunihito Hoki, " )
  TEXT( "6.0 (Engine for CSA Shogi) - May 2011" );

static LPCTSTR lpszHelpINFO
= TEXT( "Top panel shows predicted move sequence " )
  TEXT( "or opening book statistics." );

static LPCTSTR lpszHelpScore 
= TEXT( "Score: Score of the positon.  " )
  TEXT( "+200 (-200) means that black (white) wins a pawn." );

static LPCTSTR lpszHelpSpent
= TEXT( "Time Spent: Time spent since first move was made." );

static LPCTSTR lpszHelpElapsed
= TEXT( "Elapsed: Time elapsed in second while shogi engine is thinking." );

static LPCTSTR lpszHelpMemUsed
= TEXT( "Mem Used: Allocated memory currently used in percentage." );

static LPCTSTR lpszHelpMemTotal
= TEXT( "Mem Total: Total amount of memory shogi engine allocates in " )
  TEXT( "megabyte." );

static LPCTSTR lpszHelpThrdNum
= TEXT( "Thrd Num: Number of Threads shogi engine uses." );

static LPCTSTR lpszHelpTimeCtrl
= TEXT( "Time Ctrl: a) Time limit in minute, " )
  TEXT( "b) time limit for each move in second when time (a) is up." );

static LPCTSTR lpszHelpResign
= TEXT( "Resign: lower limit of score shogi engine resigns." );

static LPCTSTR lpszHelpCPU
= TEXT( "CPU: Shogi program's share of elapsed CPU time." );

static LPCTSTR lpszHelpNPS
= TEXT( "NPS: Searched nodes per second.  Indicates " )
  TEXT( "computational efficiency on the running system." );

static LPCTSTR lpszHelpMoveNow
= TEXT( "Move Now!: Force shogi engine to make an immediate move." );

static LPCTSTR lpszHelpShow
= TEXT( "Hide Result: Hide top panel and score." );

static LPCTSTR lpszHelpAudio
= TEXT( "Audio: Notice that shogi engine made a move." );

static LPCTSTR lpszHelpPonder
= TEXT( "Ponder: Ponder while shogi engine is idle." );

static LPCTSTR lpszHelpNarrowBook
= TEXT( "Narrow Book: Use narrow set of opening moves." );

static LPCTSTR lpszHelpStrictTime
= TEXT( "Strict Time: Make a move within time limit." );

static const INT aWidths[2]  = { 55, -1 };
static const UCHAR BoardHirate[SizeBoard] = {
  Lance,  Knight, Silver, Gold,   King,   Gold,   Silver, Knight, Lance,
  Empty,  Bishop, Empty,  Empty,  Empty,  Empty,  Empty,  Rook,   Empty,
  Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,
  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,
  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,
  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,  Empty,
  Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,   Pawn,
  Empty,  Rook,   Empty,  Empty,  Empty,  Empty,  Empty,  Bishop, Empty,
  Lance,  Knight, Silver, Gold,   King,   Gold,   Silver, Knight, Lance,
};

static LPCSTR szPieces[Dragon+1] = {
  "* ",
  "FU", "KY", "KE", "GI", "KI", "KA", "HI", "OU",
  "TO", "NY", "NK", "NG", "* ", "UM", "RY" };


/*
 * DllMain() is an entry point into the DLL.  this function is used to
 * initialize instances when the DLL is being loaded.
 */
BOOL WINAPI
DllMain( HINSTANCE local_hinstDLL, DWORD fdwReason, LPVOID lpvReserved )
{
  if ( fdwReason == DLL_PROCESS_ATTACH )
    {
      HANDLE hThread;
      DWORD lpThreadId;
      BOOL bSuccess;
      INT i;

      /* initialize global variables */
      memcpy( BoardInitial, BoardHirate, SizeBoard );
      szPipeReadBuf[0]   = '\0';
      szInfoBuf[0]       = '\0';
      iMovesSave         = 0;
      iScore             = 0;
      iTCM               = DefaultTCM;
      iTC                = DefaultTC;
      dwStatus           = ( StatusShowResult | StatusAudio | StatusPonder );
      hinstDLL           = local_hinstDLL;

      for ( i = 0; i < NumEvents; i++ )
	{
	  hEvents[i] = CreateEvent( NULL, TRUE, FALSE, NULL );
	  if ( hEvents[i] == NULL ) { return FALSE; }
	}

      /* create a new thread for a main dialog */
      hThread = CreateThread( NULL, 0, ThreadProcMainDlg, NULL, 0,
			      &lpThreadId );
      if ( ! hThread ) { return FALSE; }
      
      bSuccess = CloseHandle( hThread );
      if ( ! bSuccess ) { return FALSE; }
    }

  return TRUE;

  UNREFERENCED_PARAMETER( lpvReserved );
}


/*
 * sikou_ini_xt0() is an exported function called by CsaXT.exe.  The function
 * is called when a game is started.
 */
__declspec(dllexport) int CsaCallConv
sikou_ini_xt0( const volatile int *c, const char *ban )
{
  const char *ban_save = ban;
  BOOL bSuccess;
  DWORD dwResult, dwWritten;
  HANDLE hTempFile;
  HRESULT hResult;
  size_t cbUsed;
  INT i, j;

  /* save chudan_flag */
  chudan_flag = c;

  /* wait for initialization of main dialog procedure */
  dwResult = WaitForSingleObject( hEvents[EventDlgUp], INFINITE );
  if ( dwResult == WAIT_FAILED ) { APIError( "WaitForSingleObject" ); }

  /* parse 'ban' string, denotes initial position of the game */
  for ( i = 0; i < SizeBoard; i++ )
    {
      if ( ( i % 9 ) == 0 )
	{
	  while ( *ban != 'P' ) { ban++; }
	  ban += 3;
	}
      j = ( i / 9 ) * 9 + 8 - ( i % 9 );
      if      ( ! memcmp( ban, "FU", 2 ) ) { Board[j] = Pawn; }
      else if ( ! memcmp( ban, "KY", 2 ) ) { Board[j] = Lance; }
      else if ( ! memcmp( ban, "KE", 2 ) ) { Board[j] = Knight; }
      else if ( ! memcmp( ban, "GI", 2 ) ) { Board[j] = Silver; }
      else if ( ! memcmp( ban, "KI", 2 ) ) { Board[j] = Gold; }
      else if ( ! memcmp( ban, "KA", 2 ) ) { Board[j] = Bishop; }
      else if ( ! memcmp( ban, "HI", 2 ) ) { Board[j] = Rook; }
      else if ( ! memcmp( ban, "OU", 2 ) ) { Board[j] = King; }
      else if ( ! memcmp( ban, "TO", 2 ) ) { Board[j] = ProPawn; }
      else if ( ! memcmp( ban, "NY", 2 ) ) { Board[j] = ProLance; }
      else if ( ! memcmp( ban, "NK", 2 ) ) { Board[j] = ProKnight; }
      else if ( ! memcmp( ban, "NG", 2 ) ) { Board[j] = ProSilver; }
      else if ( ! memcmp( ban, "UM", 2 ) ) { Board[j] = Horse; }
      else if ( ! memcmp( ban, "RY", 2 ) ) { Board[j] = Dragon; }
      else                                 { Board[j] = Empty; }

      ban +=3;
    }

  /* If the initial position differs from the previous game, tell the
     position to bonanza.exe */
  if ( memcmp( Board, BoardInitial, SizeBoard ) )
    {
      memcpy( BoardInitial, Board, SizeBoard );
      iMovesSave = 0;

      hTempFile = CreateFile( lpszBoardDat,  GENERIC_WRITE, 0, NULL,
			      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
      if ( hTempFile == INVALID_HANDLE_VALUE ) { APIError( "CreateFile" ); }
      
      hResult = StringCbLengthA( ban_save, 1024, &cbUsed );
      if ( FAILED(hResult) ) { Error( "StringCbLengthA" ); }
      
      bSuccess = WriteFile( hTempFile, ban_save, cbUsed, &dwWritten, NULL );
      if ( ! bSuccess ) { APIError( "WriteFile" ); }
      
      CloseHandle( hTempFile );
      if ( ! bSuccess ) { return FALSE; }
      
      SendMessage( hwndMainDlg, WmRead, (WPARAM)lpszBoardDat, 0 );
    }

  if ( dwStatus & StatusPonder )
    {
      SendMessage( hwndMainDlg, WmPonderOn, 0, 0 );
    }
  SendMessage( hwndMainDlg, WmGameStart, 0, 0 );

  return 1;
}


/*
 * sikou_ini() is an exported function called by Csa.exe.  this function
 * is called when a game is started.
 */
__declspec(dllexport) void CsaCallConv
sikou_ini( int *c )
{
  DWORD dwResult;

  /* save chudan_flag */
  chudan_flag = c;

  /* wait for initialization of main dialog procedure */
  dwResult = WaitForSingleObject( hEvents[EventDlgUp], INFINITE );
  if ( dwResult == WAIT_FAILED ) { APIError( "WaitForSingleObject" ); }

  /* If the initial position isn't Hirate, tell this to bonanza.exe */
  if ( memcmp( BoardInitial, BoardHirate, SizeBoard ) )
    {
      SendMessage( hwndMainDlg, WmNewBoard, 0, 0 );
      memcpy( BoardInitial, BoardHirate, SizeBoard );
      iMovesSave = 0;
    }

  if ( dwStatus & StatusPonder )
    {
      SendMessage( hwndMainDlg, WmPonderOn, 0, 0 );
    }
  SendMessage( hwndMainDlg, WmGameStart, 0, 0 );
}


/*
 * sikou() is an exported function called by Csa.exe.  The function
 * is called when side to play is computer.
 */
__declspec(dllexport) int CsaCallConv
sikou( int tesu, unsigned char kifu[][2], int *timer_sec, int *i_moto,
       int *i_saki, int *i_naru )
{
  MSG msg;
  DWORD dwResult;
  BOOL bRecordTheSame, bPonderChecked, bTheSameGame, bChudan;
  INT iMoves, iRet;
  UINT uPiece, uFileFrom, uRankFrom, uFileTo, uRankTo, uFrom, uTo;

  bRecordTheSame = TRUE;
  bChudan        = FALSE;
  bPonderChecked = FALSE;
  bTheSameGame   = FALSE;
  uPiece = uFileTo = uFileFrom = uRankTo = uRankFrom = 0;

  /* scan 'kifu' array */
  memcpy( Board, BoardInitial, SizeBoard );
  for ( iMoves = 0; iMoves < tesu; iMoves++ )
    {
      uFrom = (UINT)kifu[iMoves][1];
      if ( uFrom < 101 )
	{
	  uFrom       -= 1U;
	  uPiece       = Board[uFrom];
	  Board[uFrom] = Empty;
	}
      else { uPiece = uFrom - 100U; }

      uTo = (UINT)kifu[iMoves][0] - 1U;
      if ( uTo >= 81 )
	{
	  uPiece = uPiece + Promote;
	  uTo    = uTo    - 100;
	}
      Board[uTo] = (UCHAR)uPiece;

      if ( bRecordTheSame )
	{
	  /* if the move sequence is longer than, or differs from previous
	     computer's turn, set bRecordTheSame to be FALSE. */
	  if ( iMoves >= iMovesSave
	       || kifu[iMoves][0] != HistorySave[iMoves][0]
	       || kifu[iMoves][1] != HistorySave[iMoves][1] )
	    {
	      bRecordTheSame = FALSE;
	      /* if the move sequence differs from previous computer's turn,
		 tell this to bonanza.exe to take steps back. */
	      if ( iMoves < iMovesSave )
		{
		  if ( ! bPonderChecked && ( dwStatus & StatusPonder ) )
		    {
		      /* stop pondering while sikou.dll edits the board. */
		      bPonderChecked = TRUE;
		      SendMessage( hwndMainDlg, WmPonderOff, 0, 0 );
		    }
		  SendMessage( hwndMainDlg, WmRetractBoard, iMoves, 0 );
		}
	      /* if the move sequence is 1 ply longer than previous computer's
		 turn, still the same until iMovesSave moves, this turn and
		 previous computer's turn are in the same game. */
	      else if ( iMoves == iMovesSave && iMoves == tesu-1 )
		{
		  bTheSameGame = TRUE;
		}
	    }
	  else { continue; }
	}
      
      if ( uFrom < 81 )
	{
	  uFileFrom = uFrom % 9 + 1U;
	  uRankFrom = uFrom / 9 + 1U;
	}
      else {
	uFileFrom = 0;
	uRankFrom = 0;
      }
      uFileTo = uTo % 9U + 1U;
      uRankTo = uTo / 9U + 1U;

      if ( bTheSameGame ) { break; }

      if ( ! bPonderChecked && ( dwStatus & StatusPonder ) )
	{
	  /* stop pondering while sikou.dll edits the board. */
	  bPonderChecked = TRUE;
	  SendMessage( hwndMainDlg, WmPonderOff, 0, 0 );
	}

      /* the move sequence is longer than previous computer's turn.
	 a step forward the position in bonanza.exe */
      SendMessage( hwndMainDlg, WmMoveBoard,
		   MAKEWPARAM( MAKEWORD( uFileFrom, uRankFrom ),
			       MAKEWORD( uFileTo,   uRankTo ) ), uPiece );
    }
  /* if the move sequence is shorter than previous computer's turn, but move
     sequence is the same until iMovesSave moves, tell this to bonanza.exe to
     take steps back. */
  if ( bRecordTheSame && iMoves < iMovesSave )
    {
      if ( ! bPonderChecked && ( dwStatus & StatusPonder ) )
	{
	  /* stop pondering while sikou.dll edits the board. */
	  bPonderChecked = TRUE;
	  SendMessage( hwndMainDlg, WmPonderOff, 0, 0 );
	}
      SendMessage( hwndMainDlg, WmRetractBoard, iMoves, 0 );
    }
  
  SendMessage( hwndMainDlg, WmTimeRemain, timer_sec[0], timer_sec[1] );
  
  /* Csa.exe and bonanza.exe have the same position of the game.  now is the
     time let bonanza start to think. */
  iScore = -ScoreUnavailable;
  if ( bTheSameGame )
    {
      SendMessage( hwndMainDlg, WmThinkStart,
		   MAKEWPARAM( MAKEWORD( uFileFrom, uRankFrom ),
			       MAKEWORD( uFileTo,   uRankTo ) ), uPiece );
    }
  else { SendMessage( hwndMainDlg, WmThinkStart, 0, 0 ); }

  for (;;)
    {
      /* read all of messages in the next loop, removing each message as we
	 read it. */
      while ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
	{
	  TranslateMessage( &msg );
	  DispatchMessage( &msg );
	}
      /* if the chudan_flag is a negative integer, exit the process */
      if ( *chudan_flag < 0 ) { ExitProcess( 0 ); }

      /* if the chudan_flag is not zero, let bonanza.exe finish to think
	 immediately. */
      if ( *chudan_flag && ! bChudan )
	{
	  SendMessage( hwndMainDlg, WM_COMMAND,
		       MAKEWPARAM( ID_MOVE_NOW, 0 ), 0 );
	  bChudan = TRUE;
	}
      
      /* wait for any mesage sent or posted to the queue, or a event,
	 hEvents[EventNotThinking], is set to be signaled. */
      dwResult = MsgWaitForMultipleObjects( 1, hEvents+EventNotThinking, FALSE,
					    INFINITE, QS_ALLINPUT );
      if ( dwResult == WAIT_FAILED )
	{
	  APIError( "MsgWaitForMultipleObjects" );
	}
      /* if the event is set to be signaled, break from the loop */
      if ( dwResult == WAIT_OBJECT_0 ) { break; }
    }

  if ( bPonderChecked )
    {
      /* set pondering on */
      SendMessage( hwndMainDlg, WmPonderOn, 0, 0 );
    }

  /* if the game is ended, set the return-value to be zero. */
  if ( *szComMove == 'R' )
    {
      SendMessage( hwndMainDlg, WmNewBoard, 0, 0 );
      iMovesSave = 0;
      iRet = 0;
    }
  else {
    /* if score is unavailable, set the return-value to be ScoreUnavailable. */
    if      ( iScore == -ScoreUnavailable ) { iRet = -ScoreUnavailable; }
    /* if score indicates superior repetition for black (white), set the
       return-value to be +(-)ScoreRep. */
    else if ( iScore == abs(ScoreRep) )    { iRet = iScore; }
    else if ( iScore >  abs(ScoreRep) )    { Error( "Out of Bounce" ); }
    /* if absolute value of score is greater than or equals to ScoreMateL, set
       the return-value to be +(-)ScoreMateU. */
    else if ( iScore >=  ScoreMateL )      { iRet = ScoreMateU; }
    else if ( iScore <= -ScoreMateL )      { iRet = -ScoreMateU; }
    /* set the return-value to be iScore */
    else                                   { iRet = iScore; }
    iRet += 1 + ScoreUnavailable;

    /* set evaluated move by bonanza.exe to i_moto, i_saki and i_naru */
    *i_naru   = 0;
    uFileFrom = szComMove[0] - '0';
    uRankFrom = szComMove[1] - '0';
    uFileTo   = szComMove[2] - '0';
    uRankTo   = szComMove[3] - '0';

    /* find name of piece to move */
    for ( uPiece = 1; uPiece < Dragon; uPiece++ )
      {
	if ( szPieces[uPiece][0] == szComMove[4]
	     && szPieces[uPiece][1] == szComMove[5] ) { break; }
      }

    /* move */
    if ( uFileFrom | uRankFrom )
      {
	*i_moto = (INT)( uFileFrom + 9U * ( uRankFrom - 1U ) );
	/* promotion */
	if ( Board[ *i_moto - 1 ] != uPiece ) { *i_naru = 1; }
      }
    /* drop */
    else { *i_moto = (int)uPiece + 100; }
    
    *i_saki = (INT)( uFileTo + 9U * ( uRankTo - 1U ) );

    /* store HistorySave with sequence of move */
    iMovesSave = tesu + 1;
    memcpy( HistorySave, kifu, tesu*2 );
    HistorySave[tesu][1] = (UCHAR)(*i_moto);
    HistorySave[tesu][0] = (UCHAR)(*i_saki);
    if ( *i_naru ) { HistorySave[tesu][0] += 100; }
  }
  SendMessage( hwndMainDlg, WmThinkEnd, 0, 0 );

  return iRet;
}


/*
 * sikou_end() is an exported function called by Csa.exe.  this function is
 * called when a game is ended.
 */
__declspec(dllexport) void CsaCallConv
sikou_end( void )
{
  SendMessage( hwndMainDlg, WmGameEnd, 0, 0 );
  SendMessage( hwndMainDlg, WmPonderOff, 0, 0 );
}


/*
 * ThreadProcMainDlg() is a function serves as a starting address for a
 * thread.  this thread basically creates another thread, ThreadProcPipeRead,
 * and manages the main dialog.
 */
static DWORD WINAPI WINAPI
ThreadProcMainDlg( LPVOID lpParameter )
{ 
  WNDCLASS winc;
  FILETIME ft1, ft2, ft3, ft4;
  DWORD lpThreadId, dw, dwMax;
  HANDLE hChildStdinRd, hChildStdoutRd, hChildStdoutWr, hThread;
  BOOL bSuccess;
  HWND hwndTemp;
  INT iTemp;
  LPTSTR lpszTemp = (LPTSTR)abTempBuffer;
  LRESULT lResult;
  HRESULT hResult;
  MSG msg;

  /* test GetThreadTimes() function.   note that the API function is available
     to WinNT-family only. */
  bSuccess = GetThreadTimes( GetCurrentThread(), &ft1, &ft2, &ft3, &ft4 );
  if ( bSuccess ) { dwStatus |= StatusThreadTimeOK; }

  /* register a superclass of button */
  bSuccess = GetClassInfo( NULL, TEXT("BUTTON"), &winc );
  if ( ! bSuccess ) { APIError( "GetClassInfo" ); }
  wpOrigButtonProc   = winc.lpfnWndProc;
  winc.lpfnWndProc   = HButtonProc;
  winc.hInstance     = hinstDLL;
  winc.lpszClassName = TEXT( "HBUTTON" );
  if ( ! RegisterClass( &winc ) ) { APIError( "RegisterClass" ); }

  /* register a superclass of static control */
  bSuccess = GetClassInfo( NULL, TEXT("STATIC"), &winc );
  if ( ! bSuccess ) { APIError( "GetClassInfo" ); }
  wpOrigStaticProc   = winc.lpfnWndProc;
  winc.lpfnWndProc   = HStaticProc;
  winc.hInstance     = hinstDLL;
  winc.lpszClassName = TEXT( "HSTATIC" );
  if ( ! RegisterClass( &winc ) ) { APIError( "RegisterClass" ); }

  /* register a window class of a custom dialog box for the main dialog. */
  bSuccess = GetClassInfo( NULL, (LPCTSTR)32770, &winc );
  if ( ! bSuccess ) { APIError( "GetClassInfo" ); }

  winc.style        |= CS_NOCLOSE;
  winc.lpfnWndProc   = MainDlgProc;
  winc.hInstance     = hinstDLL;
  winc.hIcon         = LoadIcon( hinstDLL, MAKEINTRESOURCE(ICON_MAIN) );
  winc.lpszClassName = TEXT( "DLG_MAIN" );
  if ( ! RegisterClass( &winc ) ) { APIError( "RegisterClass" ); }

  /* create the custom dialog box */
  hwndMainDlg = CreateDialog( hinstDLL, MAKEINTRESOURCE( DLG_MAIN ), 0, NULL );
  if ( hwndMainDlg == NULL) { APIError( "CreateDialog" ); }
  
  /* create child process with anonymous pipes */
  CreateProcessAndPipe( &hChildStdinRd, &hChildStdinWr,
			&hChildStdoutRd, &hChildStdoutWr );

  /* read a title of the main dialog */
  if ( ! ReadLine( hChildStdoutRd ) ) { Error( "ReadLine" ); }
  bSuccess = SetWindowTextA( hwndMainDlg, szPipeLineBuf );
  if ( ! bSuccess ) { APIError( "SetWindowTextA" ); }

  /* create a pipe-reader thread */
  hThread = CreateThread( NULL, 0, ThreadProcPipeRead, &hChildStdoutRd, 0,
			  &lpThreadId );
  if ( ! hThread ) { APIError( "CreateThread" ); }

  bSuccess = CloseHandle( hThread );
  if ( ! bSuccess ) { return FALSE; }

  /* initialize the child process */
  WritePipe( lpszLimitTimeDD, iTCM, iTC );
  WritePipe( lpszLimitTimeS, "extendable" );
  WritePipe( lpszResignD, DefaultResign );

  /* disable some controls */
  if ( ! ( dwStatus & StatusThreadTimeOK ) )
    {
      hwndTemp = GetDlgItem( hwndMainDlg, ID_CPU1 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );

      hwndTemp = GetDlgItem( hwndMainDlg, ID_CPU2 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );
    }
      
  /* initialize a combo box, Resign. */
#if DefaultResign != 1500
#  error "DefaultResign and default cursel of ID_RSGN2 differ."
#endif
  lResult = SendDlgItemMessage( hwndMainDlg, ID_RSGN2, CB_ADDSTRING, 0,
				(LPARAM)TEXT("1000") );
  if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }

  lResult = SendDlgItemMessage( hwndMainDlg, ID_RSGN2, CB_ADDSTRING, 0,
				(LPARAM)TEXT("1500") );
  if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }

  lResult = SendDlgItemMessage( hwndMainDlg, ID_RSGN2, CB_ADDSTRING, 0,
				(LPARAM)TEXT("MATE") );
  if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }

  lResult = SendDlgItemMessage( hwndMainDlg, ID_RSGN2, CB_SETCURSEL, 1, 0 );
  if ( lResult != 1 ) { Error( "CB_SETCURSEL" ); }
  
  /* initialize a combo box, Time Ctrl. */
  lResult = SendDlgItemMessage( hwndMainDlg, ID_TC2M, CB_ADDSTRING, 0,
				(LPARAM)TEXT("0") );
  if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }

  lResult = SendDlgItemMessage( hwndMainDlg, ID_TC2M, CB_ADDSTRING, 0,
				(LPARAM)TEXT("25") );
  if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }

  lResult = SendDlgItemMessage( hwndMainDlg, ID_TC2M, CB_ADDSTRING, 0,
				(LPARAM)TEXT("55") );
  if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }

  /* note: the text of list should be the same as DefaultTCM */
  lResult = SendDlgItemMessage( hwndMainDlg, ID_TC2M, CB_SETCURSEL, 0, 0 );
  if ( lResult ) { Error( "CB_SETCURSEL" ); }

  for ( iTemp = 0; iTemp <= 60; iTemp++ )
    {
      hResult = StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%u"), iTemp );
      if ( FAILED(hResult) ) { Error( "StringCbPrintf" ); }
      lResult = SendDlgItemMessage( hwndMainDlg, ID_TC2, CB_ADDSTRING, 0,
				    (LPARAM)lpszTemp );
      if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }
    }
  lResult = SendDlgItemMessage( hwndMainDlg, ID_TC2, CB_SETCURSEL, iTC, 0 );
  if ( lResult != iTC ) { Error( "CB_SETCURSEL" ); }
  
  /* initialize a combo box, Mem Used. */
  dw    = HashSizeMin;
  dwMax = GetTotalPhys() * 3U / 4U;
  for ( iTemp = 0; ; iTemp++ )
    {
      hResult = StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%u"),
				dw + MemUsedBase );
      if ( FAILED(hResult) ) { Error( "StringCbPrintf" ); }
      lResult = SendDlgItemMessage( hwndMainDlg, ID_MEM_TTL2, CB_ADDSTRING, 0,
				    (LPARAM)lpszTemp );
      if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }
      dw *= 2;
      if ( dw > dwMax ) { break; }
    }
  if ( iTemp > 1 ) { iTemp = 1; }
  lResult = SendDlgItemMessage( hwndMainDlg, ID_MEM_TTL2, CB_SETCURSEL, iTemp,
				0 );
  if ( lResult != iTemp ) { Error( "CB_SETCURSEL" ); }

  /* initialize a combo box, Thrd Num. */
  for ( iTemp = 1; iTemp <= 12; iTemp++ )
    {
      hResult = StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%d"), iTemp );
      if ( FAILED(hResult) ) { Error( "StringCbPrintf" ); }
      lResult = SendDlgItemMessage( hwndMainDlg, ID_TLP_NUM2, CB_ADDSTRING, 0,
				    (LPARAM)lpszTemp );
      if ( lResult < 0 ) { Error( "CB_ADDSTRING" ); }
    }
  lResult = SendDlgItemMessage( hwndMainDlg, ID_TLP_NUM2, CB_SETCURSEL, 0, 0 );
  if ( lResult ) { Error( "CB_SETCURSEL" ); }

  /* initialize buttons, Audio and Ponder */
  SendDlgItemMessage( hwndMainDlg, ID_AUDIO, BM_SETCHECK,
		      (WPARAM)BST_CHECKED, 0 );
  SendDlgItemMessage( hwndMainDlg, ID_PONDER, BM_SETCHECK,
		      (WPARAM)BST_CHECKED, 0 );

  /* the dialog is up.  set hEvents[EventDlgUp] to signaled */
  bSuccess = SetEvent( hEvents[EventDlgUp] );
  if ( ! bSuccess ) { APIError( "SetEvent" ); }

  /* the next message loop removes and sends messages to the custom-dialog
     procedures */
  for ( ;; )
    {
      bSuccess = GetMessage( &msg, NULL, 0, 0 );
      if ( bSuccess == -1 ) { APIError( "GetMessage" ); }
      if ( bSuccess ==  0 ) { break; }

      TranslateMessage( &msg );
      DispatchMessage( &msg );
    }

  ExitThread( TRUE );

  UNREFERENCED_PARAMETER( lpParameter );
} 


/*
 * ThreadProcPipeRead() is a function serves as a starting address for a
 * thread.  the thread keeps on reading standard output of bonanza.exe
 */
static DWORD WINAPI
ThreadProcPipeRead( LPVOID lpParameter )
{
  HANDLE hChildStdoutRd = *(LPHANDLE)lpParameter;
  double dTemp;
  LPSTR lpsz;
  BOOL bSuccess;

  /* the next loop keeps on reading a command line from standard output of
     bonanza.exe, parse the command, and pass a message to the custom
     dialog. */
  while ( ReadLine( hChildStdoutRd ) )
    { 
      /* a move was made. */
      if ( ! memcmp( szPipeLineBuf, "move", 4 ) )
	{
	  StringCchCopyA( szComMove, SizeComMove, szPipeLineBuf+4 );
	  bSuccess = SetEvent( hEvents[EventNotThinking] );
	  if ( ! bSuccess ) { APIError( "SetEvent" ); }
	}
      /* bonanza.exe found that the present board is already ended, or decided
         to resign the game. */
      else if ( ! strcmp( szPipeLineBuf,
			  "ERROR: move after a game was concluded" )
		|| ! strcmp( szPipeLineBuf, lpszResign ) )
	{
	  *szComMove = 'R';
	  bSuccess = SetEvent( hEvents[EventNotThinking] );
	  if ( ! bSuccess ) { APIError( "SetEvent" ); }
	}
      else if ( ! memcmp( szPipeLineBuf, "info", 4 ) )
	{
	  if ( ! strcmp( szPipeLineBuf+4, " ponder start" ) )
	    {
	      /* bonanza.exe started to ponder */
	      SendMessage( hwndMainDlg, WmPonderStart, 0, 0 );
	    }
	  else if ( ! strcmp( szPipeLineBuf+4, " ponder end" ) )
	    {
	      /* bonanza.exe stoped to ponder */
	      SendMessage( hwndMainDlg, WmPonderEnd, 0, 0 );
	    }
	  else if ( ! memcmp( szPipeLineBuf+4, " tt ", 4 ) )
	    {
	      /* get time spent */
	      lpsz = szPipeLineBuf + 8;
	      StringCchCopyA( szTimeSpentBuf, SizeInfoBuf-8, lpsz );
	      SendMessage( hwndMainDlg, WmChangeTimeSpent, 0, 0 );
	    }
	  else if ( szPipeLineBuf[4] == '+' || szPipeLineBuf[4] == '-' )
	    {
	      /* parse the string szPipeLineBuf as a predicted move
		 sequence. */
	      lpsz = strchr( szPipeLineBuf+4, ' ' );
	      if ( lpsz )
		{
		  *lpsz++ = '\0';
		  StringCchCopyA( szInfoBuf, szPipeLineBuf-lpsz+SizeInfoBuf,
				  lpsz );
		}
	      else { szInfoBuf[0] = '\0'; }
	      iScore = (INT)( 100.0 * atof(szPipeLineBuf+4) );
	      SendMessage( hwndMainDlg, WmChangeInfo, 0, 0 );
	    }
	  else {
	    /* parse the string szPipeLineBuf as an opening book statistics */
	    iScore = 0;
	    lpsz   = szPipeLineBuf + 4;
	    StringCchCopyA( szInfoBuf, SizeInfoBuf-4, lpsz );
	    SendMessage( hwndMainDlg, WmChangeInfo, 0, 0 );
	  }
	}
      /* bonanza.exe dumped some properties, that is, hash saturation, share
	 of cpu time, and searched nodes per a second. */
      else if ( ! memcmp( szPipeLineBuf, "stat", 4 ) )
	{
	  if ( sscanf_s( szPipeLineBuf+4, "satu=%d cpu=%d nps=%lf",
			 &iMem, &iCPU, &dTemp ) != 3 )
	    {
	      Error( "INTERNAL ERROR" );
	    }
	  iNPS = (INT)( dTemp * 1000.0 );
	  SendMessage( hwndMainDlg, WmChangeStat, 0, 0 );
	}
      else if ( ! memcmp( szPipeLineBuf, "WARNING", 7 ) )
	{
	  /* we got a warning message. */
	  StringCchCopyA( szMainDlgStatus, SizeMainDlgStatus, szPipeLineBuf );
	  SendMessage( hwndMainDlg, WmChangeMainDlgStatus, 0, 0 );
	}
      else if ( ! memcmp( szPipeLineBuf, "ERROR", 5 )
		|| ! memcmp( szPipeLineBuf, "FATAL ERROR", 11 ) )
	{
	  /* oops, we got an error message. */
	  ErrorA( szPipeLineBuf );
	}
    } 

  return 1;

  UNREFERENCED_PARAMETER( lpParameter );
}


/*
 * MainDlgProc() is a window procdure for a custom dialog box.
 */
static LRESULT CALLBACK
MainDlgProc( HWND hWnd , UINT uMsg, WPARAM wParam, LPARAM lParam )
{
  LPTSTR lpszTemp = (LPTSTR)abTempBuffer;
  LPSTR lpszTempA = (LPSTR)abTempBuffer;
  LPSTR lpszA;
  UINT ui, uiMin, uiSec;
  INT iTemp1, iTemp2;
  DWORD dw;
  BOOL bSuccess;
  HWND hwndTemp;
  COLORREF cr;
  HBRUSH hBrush;
  INITCOMMONCONTROLSEX icc;
  LRESULT lResult;
  LONG l;

  switch ( uMsg )
    {
    case WM_CREATE:
      /* create a status bar */
      icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
      icc.dwICC  = ICC_BAR_CLASSES;
      bSuccess   = InitCommonControlsEx( &icc );
      if ( ! bSuccess ) { APIError( "InitCommonControlsEx" ); }
      hwndTemp = CreateWindow( STATUSCLASSNAME, lpszReady,
			       WS_CHILD | WS_VISIBLE | CCS_BOTTOM,
			       CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, hWnd,
			       (HMENU)ID_STATUS,
			       ((LPCREATESTRUCT)lParam)->hInstance, NULL );
      if ( ! hwndTemp ) { APIError( "CreateWindow" ); }
      lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_SETPARTS, 2,
				    (LPARAM)aWidths );
      if ( ! lResult ) { APIError( "SB_SETPARTS" ); }
      return 0;

    case WM_COMMAND:
      switch ( LOWORD(wParam) )
	{
	case ID_MOVE_NOW:
	  /* send 's' command to bonanza.exe */
	  WritePipe( lpszMoveNow );
	  break;

	case ID_SHOW:
	  /* toggle a static control ID_SCORE1 between enabled and disabled */
	  dwStatus ^= StatusShowResult;
	  if ( dwStatus & StatusShowResult )
	    {
	      hwndTemp = GetDlgItem( hWnd, ID_SCORE1 );
	      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
	      EnableWindow( hwndTemp, TRUE );
	    }
	  else {
	    hwndTemp = GetDlgItem( hWnd, ID_SCORE1 );
	    if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
	    EnableWindow( hwndTemp, FALSE );
	  }
	  SendMessage( hWnd, WmChangeInfo, 0, 0 );
	  break;

	case ID_AUDIO:
	  dwStatus ^= StatusAudio;
	  break;

	case ID_PONDER:
	  dwStatus ^= StatusPonder;
	  if ( dwStatus & StatusPonder ) { WritePipe( lpszPonderOn ); }
	  else                           { WritePipe( lpszPonderOff ); }
	  break;

	case ID_NRWBK:
	  dwStatus ^= StatusNarrowBook;
	  if ( dwStatus & StatusNarrowBook ) { WritePipe( lpszBookNarrow ); }
	  else                               { WritePipe( lpszBookWide ); }
	  break;

	case ID_STRCTT:
	  dwStatus ^= StatusStrictTime;
	  WritePipe( lpszLimitTimeS,
		     ( dwStatus & StatusStrictTime )
		     ? "strict" : "extendable" );
	  break;

	case ID_RSGN2:
	  /* change a window text of the combo box, Resign, when the control
	     loses the keyboard focus */
	  if ( HIWORD(wParam) == CBN_KILLFOCUS )
	    {
	      hwndTemp = (HWND)lParam;
	      
	      iTemp1 = GetWindowTextA( hwndTemp, lpszTempA, SizeTempBuf );
	      if ( ! iTemp1 && GetLastError() != ERROR_SUCCESS )
		{
		  APIError( "GetWindowText" );
		}

	      if ( ! strcmp( lpszTempA, "MATE" ) )
		{
		  iTemp1 = ScoreUnavailable;
		}
	      else {
		l = strtol( lpszTempA, &lpszA, 0 );
		/* use the default value, since a string obtained
		   from the control is not in acceptable form as a number. */
		if      ( lpszA == lpszTempA ) { iTemp1 = DefaultResign; }
		/* if the string is smaller than MinResign, set it to be
		   MinResign. */
		else if ( l < MinResign )      { iTemp1 = MinResign; }
		else                           { iTemp1 = l; }

		StringCbPrintfA( lpszTempA, SizeTempBuf, "%d", iTemp1 );
		bSuccess = SetWindowTextA( hwndTemp, lpszTempA );
		if ( ! bSuccess ) { APIError( "SetWindowTextA" ); }
	      }

	      /* send 'limit time' command to bonanza.exe */
	      WritePipe( lpszResignD, iTemp1 );
	    }
	  break;
	  
	case ID_TC2M:
	  /* change a window text of the combo box, Time Ctrl a),
	     when the control loses the keyboard focus */
	  if ( HIWORD(wParam) == CBN_KILLFOCUS )
	    {
	      hwndTemp = (HWND)lParam;
	      
	      iTemp1 = GetWindowTextA( hwndTemp, lpszTempA, SizeTempBuf );
	      if ( ! iTemp1 && GetLastError() != ERROR_SUCCESS )
		{
		  APIError( "GetWindowText" );
		}

	      l = strtol( lpszTempA, &lpszA, 0 );
	      /* set 'iTC' to be the default value, since a string obtained
		 from the control is not in acceptable form as a number. */
	      if      ( lpszA == lpszTempA ) { iTCM = DefaultTCM; }
	      else                           { iTCM = l; }

	      StringCbPrintfA( lpszTempA, SizeTempBuf, "%d", iTCM );
	      bSuccess = SetWindowTextA( hwndTemp, lpszTempA );
	      if ( ! bSuccess ) { APIError( "SetWindowTextA" ); }

	      /* send 'limit time' command to bonanza.exe */
	      if ( iTCM | iTC ) { WritePipe( lpszLimitTimeDD, iTCM, iTC ); }
	      else              { WritePipe( lpszLimitTimeDD, 0, 1 ); }
	    }
	  break;
	  
	case ID_TC2:
	  /* change a window text of the combo box, Time Ctrl b),
	     when the control loses the keyboard focus */
	  if ( HIWORD(wParam) == CBN_KILLFOCUS )
	    {
	      hwndTemp = (HWND)lParam;
	      
	      iTemp1 = GetWindowTextA( hwndTemp, lpszTempA, SizeTempBuf );
	      if ( ! iTemp1 && GetLastError() != ERROR_SUCCESS )
		{
		  APIError( "GetWindowText" );
		}

	      l = strtol( lpszTempA, &lpszA, 0 );
	      /* set 'iTC' to be the default value, since a string obtained
		 from the control is not in acceptable form as a number. */
	      if      ( lpszA == lpszTempA ) { iTC = DefaultTC; }
	      else                           { iTC = l; }

	      StringCbPrintfA( lpszTempA, SizeTempBuf, "%d", iTC );
	      bSuccess = SetWindowTextA( hwndTemp, lpszTempA );
	      if ( ! bSuccess ) { APIError( "SetWindowTextA" ); }

	      /* send 'limit time' command to bonanza.exe */
	      if ( iTCM | iTC ) { WritePipe( lpszLimitTimeDD, iTCM, iTC ); }
	      else              { WritePipe( lpszLimitTimeDD, 0, 1 ); }
	    }
	  break;
	  
	case ID_MEM_TTL2:
	  /* send 'hash' command to bonanza.exe when a current selection of
	     the list box is changed */
	  if ( HIWORD(wParam) == CBN_SELCHANGE )
	    {
	      hwndTemp = (HWND)lParam;
	      lResult = SendMessage( hwndTemp, CB_GETCURSEL, 0, 0 );
	      if ( lResult == CB_ERR ) { Error( "CB_GETCURSEL" ); }
	      iTemp1 = (INT)lResult;
	      WritePipe( lpszHashD, iTemp1 + HashBase );
	    }
	  break;

	case ID_TLP_NUM2:
	  /* send 'tlp num' command to bonanza.exe when a current selection of
	     the list box is changed */
	  if ( HIWORD(wParam) == CBN_SELCHANGE )
	    {
	      hwndTemp = (HWND)lParam;
	      lResult = SendMessage( hwndTemp, CB_GETCURSEL, 0, 0 );
	      if ( lResult == CB_ERR ) { Error( "CB_GETCURSEL" ); }
	      iTemp1 = (INT)lResult;
	      WritePipe( lpszTLPNumD, iTemp1+1 );
	    }
	  break;
	}
      return 0;

    case WM_CTLCOLORSTATIC:
      /* the following bunch of codes basically modifies background color of
	 static controls. */
      dw = dwStatus & StatusShowResult;
      l  = GetWindowLong( (HWND)lParam, GWL_ID );
      if ( ! l ) { APIError( "GetWindowLog" ); }

      if ( l == ID_ICON_BG )
	{
	  hBrush = GetStockObject( BLACK_BRUSH );
	  if ( ! hBrush ) { APIError( "GetStockObject" ); }
	}
      else if ( l == ID_MEM2 || l == ID_TIME2 || l == ID_TIME2M || l == ID_NPS2
		|| ( l == ID_CPU2  && ( dwStatus & StatusThreadTimeOK ) )
		|| ( l == ID_INFO   && dw )
		|| ( l == ID_SCORE2 && dw ) )
	{
	  cr = SetBkColor( (HDC)wParam, RGB( 255, 255, 255 ) );
	  if ( cr == CLR_INVALID ) { APIError( "SetBkColor" ); }

	  hBrush = GetStockObject( WHITE_BRUSH );
	  if ( ! hBrush ) { APIError( "GetStockObject" ); }
	}
      else {
	cr = SetBkColor( (HDC)wParam, GetSysColor( COLOR_3DFACE ) );
	if ( cr == CLR_INVALID ) { APIError( "SetBkColor" ); }
	
	hBrush = GetSysColorBrush( COLOR_3DFACE );
	if ( ! hBrush ) { APIError( "GetStockObject" ); }
      }
      return (LRESULT)hBrush;
	
    case WM_TIMER:
      /* renew a window text of the static control 'Elapsed' at a second
	 interval */
      ui    = GetTime() - uiTimeStart;
      uiMin = ui / 60000U;
      uiSec = ui / 1000U;
      if ( uiMin )
	{
	  StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%2u:%02u"),
			   uiMin, uiSec % 60 );
	}
      else {
	StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%.2f"),
			(double)(uiSec*1000+uiMin)/1000.0 );
      }
      bSuccess = SetDlgItemText( hWnd, ID_TIME2, lpszTemp );
      if ( ! bSuccess ) { APIError( "SetDlgItemText" ); }
      return 0;

    case WmChangeMainDlgStatus:
      /* set a text of the status bar */
      lResult = SendDlgItemMessageA( hWnd, ID_STATUS, SB_SETTEXT,
				     SBPartMessage, (LPARAM)szMainDlgStatus );
      if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
      return 0;

    case WmChangeTimeSpent:
      bSuccess = SetDlgItemTextA( hWnd, ID_TIME2M, szTimeSpentBuf );
      if ( ! bSuccess ) { APIError( "SetDlgItemTextA" ); }
      return 0;

    case WmChangeInfo:
      /* set texts of static controls, 'INFO' and 'SCORE' */
      dw = dwStatus & StatusShowResult;

      bSuccess = SetDlgItemTextA( hWnd, ID_INFO, dw ? szInfoBuf : "" );
      if ( ! bSuccess ) { APIError( "SetDlgItemTextA" ); }

      /*
       * assign a number, iScore, to a text shown in 'Score' static control
       * as:
       *  32599           - superior repetition for black
       *  32598 ~  30001  - black mates white
       *  30000 ~ -30000  - an estimated score: +100 (-100) means that black
       *                    (white) wins a pawn.
       * -30001 ~ -32598  - white mates black
       * -32599           - superior repetition for white
       *
       */
      if ( iScore > abs(ScoreRep) ) { Error( "Out of Bounce" ); }
      else if ( iScore == ScoreRep )
	{
	  StringCbCopy( lpszTemp, SizeTempBuf, TEXT("+Repeat") );
	}
      else if ( iScore >= ScoreMateL )
	{
	  StringCbCopy( lpszTemp, SizeTempBuf, TEXT("+Mate") );
	}
      else if ( iScore == -ScoreRep )
	{
	  StringCbCopy( lpszTemp, SizeTempBuf, TEXT("-Repeat") );
	}
      else if ( iScore <= -ScoreMateL )
	{
	  StringCbCopy( lpszTemp, SizeTempBuf, TEXT("-Mate") );
	}
      else { StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%+d"), iScore ); }

      bSuccess = SetDlgItemText( hWnd, ID_SCORE2, dw ? lpszTemp : TEXT("") );
      if ( ! bSuccess ) { APIError( "SetWindowText" ); }
      return 0;

    case WmChangeStat:
      /* renew 'CPU' static control */
      if ( dwStatus & StatusThreadTimeOK )
	{
	  StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%d%%"), iCPU );
	  bSuccess = SetDlgItemText( hWnd, ID_CPU2, lpszTemp );
	  if ( ! bSuccess ) { APIError( "SetWindowText" ); }
	}

      /* renew 'Mem Used' static control */
      StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%d%%"), iMem );
      bSuccess = SetDlgItemText( hWnd, ID_MEM2, lpszTemp );
      if ( ! bSuccess ) { APIError( "SetWindowText" ); }

      /* renew 'NPS' static control */
      if ( iNPS < 1000000 )
	{
	  StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%dK"),
			  ( iNPS + 500 ) / 1000 );
	}
      else {
	StringCbPrintf( lpszTemp, SizeTempBuf, TEXT("%.2fM"),
			(double)iNPS / 1000000.0 );
      }
      bSuccess = SetDlgItemText( hWnd, ID_NPS2, lpszTemp );
      if ( ! bSuccess ) { APIError( "SetWindowText" ); }
      return 0;

    case WmPonderOn:
      WritePipe( lpszPonderOn );
      return 0;

    case WmPonderOff:
      WritePipe( lpszPonderOff );
      return 0;

    case WmNewBoard:
      /* send 'new' command to bonanza.exe */
      WritePipe( lpszNew );
      return 0;

    case WmRead:
      /* send 'read filename' command to bonanza.exe */
      WritePipe( lpszReadS, wParam );
      return 0;

    case WmRetractBoard:
      /* send 'read . t number' command to bonanza.exe */
      WritePipe( lpszReadD, (INT)wParam + 1 );
      return 0;

    case WmMoveBoard:
      /* send 'move [0-9][0-9][1-9][1-9](piece name)' command to bonanza.exe */
      WritePipe( lpszMoveBoard,
		 LOBYTE( LOWORD(wParam) ), HIBYTE( LOWORD(wParam) ),
		 LOBYTE( HIWORD(wParam) ), HIBYTE( HIWORD(wParam) ),
		 szPieces[ lParam ] );
      return 0;

    case WmGameStart:
      if ( dwStatus & StatusReady )
	{
	  dwStatus &= ~StatusReady;
	  /* change a text of the status bar to 'Idling' */
	  lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_SETTEXT,
					SBPartStatus, (LPARAM)lpszIdling );
	  if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
	}
      return 0;

    case WmGameEnd:
      dwStatus &= ~( StatusPondering | StatusThinking );
      dwStatus |= StatusReady;

      /* change a text of the status bar to 'Ready' */
      lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_SETTEXT, SBPartStatus,
				    (LPARAM)lpszReady );
      if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
      return 0;

    case WmThinkStart:
      dwStatus &= ~( StatusPondering | StatusReady );
      dwStatus |= StatusThinking;

      /* reset an event, hEvents[EventNotThinking] */
      bSuccess = ResetEvent( hEvents[EventNotThinking] );
      if ( ! bSuccess ) { APIError( "ResetEvent" ); }

      /* disable a combo box, Time Ctrl, while computer is thinking */
      hwndTemp = GetDlgItem( hWnd, ID_TC2M );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );

      hwndTemp = GetDlgItem( hWnd, ID_TC2 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );

      /* disable a combo box, Thrd Num, while computer is thinking */
      hwndTemp = GetDlgItem( hWnd, ID_TLP_NUM2 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );

      /* disable a combo box, Mem Used, while computer is thinking */
      hwndTemp = GetDlgItem( hWnd, ID_MEM_TTL2 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );

      /* disable a checkbox, Strict Time, while computer is thinking */
      hwndTemp = GetDlgItem( hWnd, ID_STRCTT );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );

      /* enable 'Move Now!' button while computer is thinking */
      hwndTemp = GetDlgItem( hWnd, ID_MOVE_NOW );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, TRUE );

      /* change a text of the status bar to 'Thinking' */
      lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_SETTEXT, SBPartStatus,
				    (LPARAM)lpszThinking );
      if ( ! lResult ) { APIError( "SB_SETTEXT" ); }

      /* activate a timer to count elapsed time */
      uiTimeStart = GetTime();
      if ( ! SetTimer( hWnd, 1, 1000, NULL ) ) { APIError( "SetTimer" ); }
      
      SendMessage( hWnd, WM_TIMER, 0, 0 );

      /* send 'move' command to bonanza.exe */
      if ( lParam )
	{
	  WritePipe( "%d%d%d%d%s\n",
		     LOBYTE( LOWORD(wParam) ), HIBYTE( LOWORD(wParam) ),
		     LOBYTE( HIWORD(wParam) ), HIBYTE( HIWORD(wParam) ),
		     szPieces[ lParam ] );
	}
      else { WritePipe( lpszMove ); }

      return 0;

    case WmThinkEnd:
      if ( dwStatus & StatusThinking )
	{
	  dwStatus &= ~StatusThinking;
	  /* change a text of the status bar to 'Idling' */
	  lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_SETTEXT,
					SBPartStatus, (LPARAM)lpszIdling );
	  if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
	}

      /* enable a combo box, Time Ctrl */
      hwndTemp = GetDlgItem( hWnd, ID_TC2M );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, TRUE );

      hwndTemp = GetDlgItem( hWnd, ID_TC2 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, TRUE );

      /* enable a combo box, Thrd Num */
      hwndTemp = GetDlgItem( hWnd, ID_TLP_NUM2 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, TRUE );

      /* enable a combo box, Mem Used */
      hwndTemp = GetDlgItem( hWnd, ID_MEM_TTL2 );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, TRUE );

      /* enable a checkbox, Strict Time */
      hwndTemp = GetDlgItem( hWnd, ID_STRCTT );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, TRUE );

      /* disable 'Move Now!' button */
      hwndTemp = GetDlgItem( hWnd, ID_MOVE_NOW );
      if ( ! hwndTemp ) { APIError( "GetDlgItem" ); }
      EnableWindow( hwndTemp, FALSE );

      /* kill a timer */
      if ( ! KillTimer( hWnd, 1 ) ) { APIError( "KillTimer" ); }

      /* beep if button 'Audio' is checked */
      if ( dwStatus & StatusAudio ) { MessageBeep( MB_ICONEXCLAMATION ); }
      return 0;

    case WmPonderStart:
      dwStatus &= ~( StatusThinking | StatusReady );
      dwStatus |= StatusPondering;

      /* change a text of the status bar to 'Pondering' */
      lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_SETTEXT,
				    SBPartStatus, (LPARAM)lpszPondering );
      if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
      return 0;

    case WmPonderEnd:
      if ( dwStatus & StatusPondering )
	{
	  dwStatus &= ~StatusPondering;
	  lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_GETTEXT,
					SBPartStatus, (LPARAM)lpszTemp );
	  if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
	}

      if ( ! lstrcmp( lpszTemp, lpszPondering ) )
	{
	  /* change a text of the status bar to 'Idling' */
	  lResult = SendDlgItemMessage( hWnd, ID_STATUS, SB_SETTEXT,
					SBPartStatus, (LPARAM)lpszIdling );
	  if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
	}
      return 0;

    case WmTimeRemain:
      iTemp1 = iTCM * 60 - (INT)wParam;
      if ( iTemp1 < 0 ) { iTemp1 = 0; }

      iTemp2 = iTCM * 60 - (INT)lParam;
      if ( iTemp2 < 0 ) { iTemp2 = 0; }

      WritePipe( lpszTimeRemainDD, iTemp1, iTemp2 );
      return 0;
    }

  return DefDlgProc( hWnd , uMsg, wParam, lParam );
}


/*
 * HStaticProc() is a window procdure for a superclass of static control.
 */
static LRESULT CALLBACK
HStaticProc( HWND hWnd , UINT uMsg, WPARAM wParam, LPARAM lParam )
{
  /* if the cursor moves into one of static controls and mouse input is not
     captured, find a help message corresponding to the static control. */ 
  if ( uMsg == WM_SETCURSOR )
    {
      LRESULT lResult;
      LPCTSTR lpszTemp;
      HWND hwndTemp;

      switch ( GetDlgCtrlID( hWnd ) )
	{
	case ID_INFO:
	  lpszTemp = lpszHelpINFO;
	  break;

	case ID_SCORE1:
	case ID_SCORE2:
	  lpszTemp = lpszHelpScore;
	  break;

	case ID_TIME1M:
	case ID_TIME2M:
	  lpszTemp = lpszHelpSpent;
	  break;

	case ID_TIME1:
	case ID_TIME2:
	  lpszTemp = lpszHelpElapsed;
	  break;

	case ID_MEM1:
	case ID_MEM2:
	  lpszTemp = lpszHelpMemUsed;
	  break;

	case ID_MEM_TTL1:
	  lpszTemp = lpszHelpMemTotal;
	  break;
	  
	case ID_TC1:
	case ID_TC1M:
	  lpszTemp = lpszHelpTimeCtrl;
	  break;

	case ID_RSGN1:
	case ID_RSGN2:
	  lpszTemp = lpszHelpResign;
	  break;

	case ID_CPU1:
	case ID_CPU2:
	  lpszTemp = lpszHelpCPU;
	  break;

	case ID_TLP_NUM1:
	case ID_TLP_NUM2:
	  lpszTemp = lpszHelpThrdNum;
	  break;

	case ID_NPS1:
	case ID_NPS2:
	  lpszTemp = lpszHelpNPS;
	  break;

	case ID_ICON_BG:
	  lpszTemp = lpszAbout;
	  break;

	default:
	  APIError( "GetDlgCtrlID" );
	}

      /* show the help message in the status bar */
      hwndTemp = GetParent( hWnd );
      if ( ! hwndTemp ) { APIError( "GetParent" ); }
      lResult = SendDlgItemMessage( hwndTemp, ID_STATUS, SB_SETTEXT,
				    SBPartMessage, (LPARAM)lpszTemp );
      if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
      return 0;
    }

  /* call a procedure function of the original window */
  return CallWindowProc( wpOrigStaticProc, hWnd, uMsg, wParam, lParam );
}


/*
 * HStaticProc() is a window procdure for a superclass of button.
 */
static LRESULT CALLBACK
HButtonProc( HWND hWnd , UINT uMsg, WPARAM wParam, LPARAM lParam )
{
  /* if the cursor moves into one of buttons and the mouse input is not
     captured, find a help message corresponding to the button. */ 
  if ( uMsg == WM_SETCURSOR )
    {
      LRESULT lResult;
      LPCTSTR lpszTemp;
      HWND hwndTemp;

      switch ( GetDlgCtrlID( hWnd ) )
	{
	case ID_MOVE_NOW:
	  lpszTemp = lpszHelpMoveNow;
	  break;

	case ID_SHOW:
	  lpszTemp = lpszHelpShow;
	  break;

	case ID_AUDIO:
	  lpszTemp = lpszHelpAudio;
	  break;

	case ID_PONDER:
	  lpszTemp = lpszHelpPonder;
	  break;

	case ID_NRWBK:
	  lpszTemp = lpszHelpNarrowBook;
	  break;

	case ID_STRCTT:
	  lpszTemp = lpszHelpStrictTime;
	  break;

	default:
	  APIError( "GetDlgCtrlID" );
	}
      /* show the help message in the status bar */
      hwndTemp = GetParent( hWnd );
      if ( ! hwndTemp ) { APIError( "GetParent" ); }
      lResult = SendDlgItemMessage( hwndTemp, ID_STATUS, SB_SETTEXT,
				    SBPartMessage, (LPARAM)lpszTemp );
      if ( ! lResult ) { APIError( "SB_SETTEXT" ); }
      return 0;
    }

  /* call procedure function of the original window */
  return CallWindowProc( wpOrigButtonProc, hWnd, uMsg, wParam, lParam );
}


/*
 * CreateProcessAndPipe() creates anonymous pipes and a child process.
 */
static VOID
CreateProcessAndPipe( LPHANDLE lphChildStdinRd, LPHANDLE lphChildStdinWr,
		      LPHANDLE lphChildStdoutRd, LPHANDLE lphChildStdoutWr )
{
  SECURITY_ATTRIBUTES saAttr; 
  PROCESS_INFORMATION pi;
  STARTUPINFO si;
  HRESULT hResult;
  HANDLE hTemp, hProcess;
  BOOL bSuccess;
  LPTSTR lpszCmdLine = (LPTSTR)abTempBuffer;

  saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES); 
  saAttr.bInheritHandle       = TRUE; 
  saAttr.lpSecurityDescriptor = NULL; 
  hProcess                    = GetCurrentProcess();
  
  /* create a pipe for the standard output of bonanza.exe  */
  bSuccess = CreatePipe( &hTemp, lphChildStdoutWr, &saAttr, 0 );
  if ( ! bSuccess ) { APIError( "CreatePipe" ); }

  /* create noninheritable read handle and close the inheritable read handle */
  bSuccess = DuplicateHandle( hProcess, hTemp, hProcess, lphChildStdoutRd,
			      0, FALSE, DUPLICATE_SAME_ACCESS );
  if( ! bSuccess ) { APIError( "DuplicateHandle" ); }
  
  bSuccess = CloseHandle( hTemp );
  if( ! bSuccess ) { APIError( "CloseHandle" ); }
  
  /* create a pipe for the standard input of bonanza.exe */
  bSuccess = CreatePipe( lphChildStdinRd, &hTemp, &saAttr, 0 );
  if ( ! bSuccess ) { APIError( "CreatePipe" ); }
  
  /* create noninheritable write handle and close the inheritable write
     handle */
  bSuccess = DuplicateHandle( hProcess, hTemp, hProcess, lphChildStdinWr,
			      0, FALSE, DUPLICATE_SAME_ACCESS );
  if( ! bSuccess ) { APIError( "DuplicateHandle" ); }
  
  bSuccess = CloseHandle( hTemp ); 
  if( ! bSuccess ) { APIError( "CloseHandle" ); }

  /* create a process */
  si.cb          = sizeof(STARTUPINFO); 
  si.lpReserved  = NULL;
  si.lpDesktop   = NULL;
  si.lpTitle     = NULL;
  si.dwFlags     = STARTF_USESTDHANDLES;
  si.cbReserved2 = 0;
  si.lpReserved2 = NULL;
  si.hStdInput   = *lphChildStdinRd;
  si.hStdOutput  = *lphChildStdoutWr;
  si.hStdError   = *lphChildStdoutWr;
  hResult = StringCbCopy( lpszCmdLine, SizeTempBuf, lpszChildCmd );
  if ( FAILED(hResult) ) { Error( "StringCchCopy" ); }

  bSuccess = CreateProcess( NULL, lpszCmdLine, NULL, NULL,
			    TRUE, DETACHED_PROCESS, NULL, NULL, &si, &pi );
  if ( ! bSuccess ) { APIError( "CreateProcess" ); }

  bSuccess = CloseHandle( pi.hProcess );
  if ( ! bSuccess ) { APIError( "CloseHandle" ); }

  bSuccess = CloseHandle( pi.hThread );
  if ( ! bSuccess ) { APIError( "CloseHandle" ); }
}


/*
 * ReadLine() reads a line from standerd output of bonanza.exe,
 * and stores szPipeLineBuf with the line.
 */
static BOOL
ReadLine( HANDLE hChildStdoutRd )
{
  LPSTR szEnd, szUsedEnd, szDestEnd;
  HRESULT hResult;
  BOOL bSuccess;
  DWORD dwRead;
  size_t cb, cbUsed, cbEmpty;

  /* next loop keeps on reading outputs from bonanza.exe to szPipeReadBuf
     until it finds a newline */
  for (;;)
    {
      szEnd = strchr( szPipeReadBuf, '\n' );
      if ( szEnd ) { break; }

      hResult = StringCbLengthA( szPipeReadBuf, SizePipeReadBuf-1, &cbUsed );
      if ( FAILED(hResult) ) { Error( "StringCbLengthA" ); }
      szUsedEnd = szPipeReadBuf   + cbUsed;
      cbEmpty   = SizePipeReadBuf - cbUsed;

      bSuccess = ReadFile( hChildStdoutRd, szUsedEnd, cbEmpty-1, &dwRead,
			   NULL );
      if ( ! bSuccess )	{ APIError( "ReadFile" ); }
      if ( ! dwRead ) { Error( "Unexpected EOF of a pipe." ); }
      szUsedEnd[dwRead] = '\0';
    }
  cb = szEnd - szPipeReadBuf;
  if ( cb < SizePipeLineBuf ) { szPipeReadBuf[cb]                = '\0'; }
  else                        { szPipeReadBuf[SizePipeLineBuf-1] = '\0'; }

  /* copy a line from szPipeReadBuf to szPipeLineBuf */
  hResult = StringCbCopyExA( szPipeLineBuf, SizePipeLineBuf, szPipeReadBuf,
			     &szDestEnd, NULL, 0 );
  if ( FAILED(hResult) ) { Error( "StringCbCopyA" ); }

  /* chomp */
  while ( --szDestEnd != szPipeLineBuf )
    {
      if ( *szDestEnd != '\r' && *szDestEnd != '\n' ) { break; }
      else { *szDestEnd = '\0'; }
    }

  /* delete the line from szPipeReadBuf */
  hResult = StringCbLengthA( szEnd+1, SizePipeReadBuf-cb-1, &cb );
  if ( FAILED(hResult) ) { Error( "StringCbLengthA" ); }
  memmove( szPipeReadBuf, szEnd+1, cb+1 );

  return TRUE;
}



/*
 * WritePipe() writes a command to standerd input of bonanza.exe
 */
static VOID
WritePipe( LPCSTR lpszFormat, ... )
{
  va_list arg;
  LPSTR lpsz = (LPSTR)abTempBuffer;
  HRESULT hResult;
  size_t size;
  BOOL bSuccess;
  DWORD dwWritten;

  va_start( arg, lpszFormat );

  hResult = StringCbVPrintfA( lpsz, SizeTempBuf, lpszFormat, arg );
  if ( FAILED(hResult) ) { Error( "StringCbVPringfA" ); }

  hResult = StringCbLengthA( lpsz, SizeTempBuf, &size );
  if ( FAILED( hResult ) ) { Error( "StringCbLengthA" ); }

  bSuccess = WriteFile( hChildStdinWr, lpsz, size, &dwWritten, NULL );
  if ( ! bSuccess ) { APIError( "WriteFile" ); }

  va_end( arg );
}


/*
 * GetTotalPhys() obtains total size of physical memory in MByte.
 */
static DWORD
GetTotalPhys( VOID )
{
  MEMORYSTATUS status;

  /* note that the API function, GlobalMemoryStatus(), requires that total
     amount of physical memory should be less than 4GByte */
  GlobalMemoryStatus( &status );

  return status.dwTotalPhys / (DWORD)( 1024 * 1024 );
}


/*
 * GetTime() function returns system time in milli-second.
 */
static UINT
GetTime( VOID )
{
  FILETIME FileTime;
  ULARGE_INTEGER uli;

  GetSystemTimeAsFileTime( &FileTime );
  uli.LowPart  = FileTime.dwLowDateTime;
  uli.HighPart = FileTime.dwHighDateTime;
  
  return (UINT)( uli.QuadPart / 10000 );
}


/*
 * FuncAPIError() function ends the process with printed last error
 * of API functions.
 */
static VOID
FuncAPIError( DWORD dwLine, LPTSTR lpszMsg )
{
  LPTSTR lpsz = (LPTSTR)abTempBuffer;
  PVOID lpMsgBuf;

  FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER
		 | FORMAT_MESSAGE_FROM_SYSTEM
		 | FORMAT_MESSAGE_IGNORE_INSERTS,
		 NULL, GetLastError(),
		 MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		 (LPTSTR)( &lpMsgBuf ), 0, NULL );

  StringCbPrintf( lpsz, SizeTempBuf,
		   TEXT(__FILE__) TEXT("(%u): Fatal Error: %s() failed.\n%s"),
		   dwLine, lpszMsg, (LPTSTR)lpMsgBuf );

  MessageBox( NULL, lpsz, lpszBonanza, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL );

  ExitProcess( 0 );
}


/*
 * FuncError() function ends the process with an error message
 */
static VOID
FuncError( DWORD dwLine, LPTSTR lpszMsg )
{
  LPTSTR lpsz = (LPTSTR)abTempBuffer;

  StringCbPrintf( lpsz, SizeTempBuf,
		  TEXT(__FILE__) TEXT("(%u): Fatal Error: %s"),
		  dwLine, lpszMsg );

  MessageBox( NULL, lpsz, lpszBonanza, MB_OK | MB_ICONERROR | MB_SYSTEMMODAL );

  ExitProcess( 0 );
}


/*
 * FuncErrorA() function is an ansi-text version of FuncError() function.
 */
static VOID
FuncErrorA( DWORD dwLine, LPSTR lpszMsg )
{
  LPSTR lpsz = (LPSTR)abTempBuffer;

  StringCbPrintfA( lpsz, SizeTempBuf, __FILE__ "(%u): Fatal Error: %s",
		    dwLine, lpszMsg );

  MessageBoxA( NULL, lpsz, "Bonanza",
	       MB_OK | MB_ICONERROR | MB_SYSTEMMODAL );

  ExitProcess( 0 );
}
