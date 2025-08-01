//-----------------------------------------------------------------------
//                       github.com/samisalreadytaken/sqdbg
//-----------------------------------------------------------------------
//

#ifndef SQDBG_NET_H
#define SQDBG_NET_H

#ifdef _WIN32
	#include <WinSock2.h>
	#include <WS2tcpip.h>
	#ifdef _DEBUG
		#include <debugapi.h>

		inline bool __IsDebuggerPresent()
		{
			return IsDebuggerPresent();
		}

		inline const char *GetModuleBaseName()
		{
			static char module[MAX_PATH];
			int len = GetModuleFileNameA( NULL, module, sizeof(module) );

			if ( len != 0 )
			{
				for ( char *pBase = module + len; pBase-- > module; )
				{
					if ( *pBase == '\\' )
						return pBase + 1;
				}

				return module;
			}

			return "";
		}
	#endif

	#pragma comment(lib, "Ws2_32.lib")

	#undef RegisterClass
	#undef SendMessage
	#undef Yield
	#undef CONST
	#undef PURE

	#undef errno
	#define errno WSAGetLastError()
	#define strerr(e) gai_strerror(e)
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <sys/fcntl.h>
	#include <arpa/inet.h>
	#include <netinet/tcp.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <errno.h>
	#include <string.h>

	#define closesocket close
	#define ioctlsocket ioctl
	#define strerr(e) strerror(e)

	typedef int SOCKET;
	#define INVALID_SOCKET -1
	#define SOCKET_ERROR -1
	#define SD_BOTH SHUT_RDWR
#endif

#ifdef _DEBUG
	class CEntryCounter
	{
	public:
		int *count;
		CEntryCounter( int *p ) : count(p) { (*count)++; }
		~CEntryCounter() { (*count)--; }
	};

	#define TRACK_ENTRIES() \
		static int s_EntryCount = 0; \
		CEntryCounter entrycounter( &s_EntryCount );
#else
	#define TRACK_ENTRIES()
#endif

void *sqdbg_malloc( unsigned int size );
void *sqdbg_realloc( void *p, unsigned int oldsize, unsigned int size );
void sqdbg_free( void *p, unsigned int size );

#ifndef SQDBG_NET_BUF_SIZE
#define SQDBG_NET_BUF_SIZE ( 16 * 1024 )
#endif

class CMessagePool
{
public:
	typedef int index_t;

#pragma pack(push, 4)
	struct message_t
	{
		index_t next;
		index_t prev;
		unsigned short len;
		char ptr[1];
	};
#pragma pack(pop)

	struct chunk_t
	{
		char *ptr;
		int count;
	};

	static const index_t INVALID_INDEX = 0x80000000;

	// Message queue is going to be less than 16 unless
	// there is many variable evaluations at once or network lag
	static const int MEM_CACHE_CHUNKS_ALIGN = 16;

	// Most messages are going to be less than 256 bytes,
	// only exceeding it on long file paths and long evaluate strings
	static const int MEM_CACHE_CHUNKSIZE = 256;

	message_t *Get( index_t index )
	{
		Assert( index != INVALID_INDEX );

		int msgIdx = index & 0x0000ffff;
		int chunkIdx = index >> 16;

		Assert( m_Memory );
		Assert( chunkIdx < m_MemChunkCount );

		chunk_t *chunk = &m_Memory[ chunkIdx ];
		Assert( msgIdx < chunk->count );

		return (message_t*)&chunk->ptr[ msgIdx * MEM_CACHE_CHUNKSIZE ];
	}

	chunk_t *m_Memory;
	int m_MemChunkCount;
	int m_ElemCount;

	index_t m_Head;
	index_t m_Tail;

	index_t NewMessage( char *pcsMsg, int nLength )
	{
		if ( !m_Memory )
		{
			m_Memory = (chunk_t*)sqdbg_malloc( m_MemChunkCount * sizeof(chunk_t) );
			AssertOOM( m_Memory, m_MemChunkCount * sizeof(chunk_t) );
			memset( (char*)m_Memory, 0, m_MemChunkCount * sizeof(chunk_t) );

			chunk_t *chunk = &m_Memory[0];
			chunk->count = MEM_CACHE_CHUNKS_ALIGN;
			chunk->ptr = (char*)sqdbg_malloc( chunk->count * MEM_CACHE_CHUNKSIZE );
			AssertOOM( chunk->ptr, chunk->count * MEM_CACHE_CHUNKSIZE );
			memset( chunk->ptr, 0, chunk->count * MEM_CACHE_CHUNKSIZE );
		}

		int requiredChunks = ( sizeof(message_t) + nLength - 1 ) / MEM_CACHE_CHUNKSIZE + 1;
		int matchedChunks = 0;
		int msgIdx = 0;
		int chunkIdx = 0;

		for (;;)
		{
			chunk_t *chunk = &m_Memory[ chunkIdx ];
			Assert( chunk->count && chunk->ptr );

			message_t *msg = (message_t*)&chunk->ptr[ msgIdx * MEM_CACHE_CHUNKSIZE ];

			if ( msg->len == 0 )
			{
				if ( ++matchedChunks == requiredChunks )
				{
					msgIdx = msgIdx - matchedChunks + 1;
					msg = (message_t*)&chunk->ptr[ msgIdx * MEM_CACHE_CHUNKSIZE ];

					Assert( nLength >= 0 );
					Assert( nLength < ( 1 << ( sizeof(message_t::len) * 8 ) ) );

					msg->next = msg->prev = INVALID_INDEX;
					msg->len = (unsigned short)nLength;
					memcpy( msg->ptr, pcsMsg, nLength );

					return ( chunkIdx << 16 ) | msgIdx;
				}
			}
			else
			{
				matchedChunks = 0;
			}

			msgIdx += ( sizeof(message_t) + msg->len - 1 ) / MEM_CACHE_CHUNKSIZE + 1;

			Assert( msgIdx < 0x0000ffff );

			if ( msgIdx < chunk->count )
				continue;

			msgIdx = 0;
			matchedChunks = 0;

			if ( ++chunkIdx >= m_MemChunkCount )
			{
				int oldcount = m_MemChunkCount;
				m_MemChunkCount += 4;
				m_Memory = (chunk_t*)sqdbg_realloc( m_Memory,
						oldcount * sizeof(chunk_t),
						m_MemChunkCount * sizeof(chunk_t) );
				AssertOOM( m_Memory, m_MemChunkCount * sizeof(chunk_t) );
				memset( (char*)m_Memory + oldcount * sizeof(chunk_t),
						0,
						(m_MemChunkCount - oldcount) * sizeof(chunk_t) );
			}

			chunk = &m_Memory[ chunkIdx ];

			if ( chunk->count == 0 )
			{
				Assert( chunk->ptr == NULL );

				chunk->count = ( requiredChunks + ( MEM_CACHE_CHUNKS_ALIGN - 1 ) ) & ~( MEM_CACHE_CHUNKS_ALIGN - 1 );
				chunk->ptr = (char*)sqdbg_malloc( chunk->count * MEM_CACHE_CHUNKSIZE );
				AssertOOM( chunk->ptr, chunk->count * MEM_CACHE_CHUNKSIZE );
				memset( chunk->ptr, 0, chunk->count * MEM_CACHE_CHUNKSIZE );
			}

			Assert( chunkIdx < 0x00007fff );
		}
	}

	void DeleteMessage( message_t *pMsg )
	{
		if ( pMsg->len == 0 )
			return;

		Assert( pMsg->len > 0 );
		Assert( m_ElemCount > 0 );
		m_ElemCount--;

		int msgIdx = ( ( sizeof(message_t) + pMsg->len +
					( MEM_CACHE_CHUNKSIZE - 1 ) ) & ~( MEM_CACHE_CHUNKSIZE - 1 ) ) / MEM_CACHE_CHUNKSIZE;

		do
		{
			pMsg->next = pMsg->prev = INVALID_INDEX;
			pMsg->len = 0;
			pMsg->ptr[0] = 0;

			pMsg = (message_t*)( (char*)pMsg + MEM_CACHE_CHUNKSIZE );
		}
		while ( --msgIdx > 0 );
	}

public:
	CMessagePool() :
		m_Memory( NULL ),
		m_MemChunkCount( 4 ),
		m_ElemCount( 0 ),
		m_Head( INVALID_INDEX ),
		m_Tail( INVALID_INDEX )
	{
	}

	~CMessagePool()
	{
		if ( m_Memory )
		{
			for ( int chunkIdx = 0; chunkIdx < m_MemChunkCount; chunkIdx++ )
			{
				chunk_t *chunk = &m_Memory[ chunkIdx ];

				for ( int msgIdx = 0; msgIdx < chunk->count; )
				{
					message_t *msg = (message_t*)&chunk->ptr[ msgIdx * MEM_CACHE_CHUNKSIZE ];
					Assert( msg->len == 0 && msg->ptr[0] == 0 );
					msgIdx += ( sizeof(message_t) + msg->len - 1 ) / MEM_CACHE_CHUNKSIZE + 1;
					DeleteMessage( msg );
				}

				sqdbg_free( chunk->ptr, chunk->count * MEM_CACHE_CHUNKSIZE );
			}

			sqdbg_free( m_Memory, m_MemChunkCount * sizeof(chunk_t) );
		}

		Assert( m_ElemCount == 0 );
	}

	void Shrink()
	{
		Assert( m_ElemCount == 0 );

		if ( !m_Memory )
			return;

		for ( int chunkIdx = 1; chunkIdx < m_MemChunkCount; chunkIdx++ )
		{
			chunk_t *chunk = &m_Memory[ chunkIdx ];

			if ( chunk->count )
			{
#ifdef _DEBUG
				for ( int msgIdx = 0; msgIdx < chunk->count; )
				{
					message_t *msg = (message_t*)&chunk->ptr[ msgIdx * MEM_CACHE_CHUNKSIZE ];
					Assert( msg->len == 0 && msg->ptr[0] == 0 );
					msgIdx += ( sizeof(message_t) + msg->len - 1 ) / MEM_CACHE_CHUNKSIZE + 1;
				}
#endif
				sqdbg_free( chunk->ptr, chunk->count * MEM_CACHE_CHUNKSIZE );

				chunk->count = 0;
				chunk->ptr = NULL;
			}
		}

		if ( m_MemChunkCount > 4 )
		{
			int oldcount = m_MemChunkCount;
			m_MemChunkCount = 4;
			m_Memory = (chunk_t*)sqdbg_realloc( m_Memory,
					oldcount * sizeof(chunk_t),
					m_MemChunkCount * sizeof(chunk_t) );
			AssertOOM( m_Memory, m_MemChunkCount * sizeof(chunk_t) );
		}
	}

	void Add( char *pcsMsg, int nLength )
	{
		index_t newMsg = NewMessage( pcsMsg, nLength );

		m_ElemCount++;

		// Add to tail
		if ( m_Tail == INVALID_INDEX )
		{
			Assert( m_Head == INVALID_INDEX );
			m_Head = m_Tail = newMsg;
		}
		else
		{
			Get(newMsg)->prev = m_Tail;
			Get(m_Tail)->next = newMsg;
			m_Tail = newMsg;
		}
	}

	template < typename T, void (T::*callback)( char *ptr, int len ) >
	void Service( T *ctx )
	{
		TRACK_ENTRIES();

		index_t msg = m_Head;

		while ( msg != INVALID_INDEX )
		{
			message_t *pMsg = Get(msg);

			Assert( pMsg->len || ( pMsg->next == INVALID_INDEX && pMsg->prev == INVALID_INDEX ) );

			if ( pMsg->len == 0 )
				break;

			// Advance before execution
			index_t next = pMsg->next;
			index_t prev = pMsg->prev;

			pMsg->next = INVALID_INDEX;
			pMsg->prev = INVALID_INDEX;

			if ( prev != INVALID_INDEX )
				Get(prev)->next = next;

			if ( next != INVALID_INDEX )
				Get(next)->prev = prev;

			if ( msg == m_Head )
			{
				// prev could be non-null on re-entry
				//Assert( prev == INVALID_INDEX );
				m_Head = next;
			}

			if ( msg == m_Tail )
			{
				Assert( next == INVALID_INDEX && prev == INVALID_INDEX );
				m_Tail = INVALID_INDEX;
			}

			(ctx->*callback)( pMsg->ptr, pMsg->len );

			Assert( Get(msg) == pMsg );

			DeleteMessage( pMsg );
			msg = next;
		}
	}

	void Clear()
	{
		index_t msg = m_Head;

		while ( msg != INVALID_INDEX )
		{
			message_t *pMsg = Get(msg);

			index_t next = pMsg->next;
			index_t prev = pMsg->prev;

			if ( prev != INVALID_INDEX )
				Get(prev)->next = next;

			if ( next != INVALID_INDEX )
				Get(next)->prev = prev;

			if ( msg == m_Head )
			{
				Assert( prev == INVALID_INDEX );
				m_Head = next;
			}

			if ( msg == m_Tail )
			{
				Assert( next == INVALID_INDEX && prev == INVALID_INDEX );
				m_Tail = INVALID_INDEX;
			}

			DeleteMessage( pMsg );
			msg = next;
		}

		Assert( m_Head == INVALID_INDEX && m_Tail == INVALID_INDEX );
	}
};

static inline bool SocketWouldBlock()
{
#ifdef _WIN32
	return WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

static inline void CloseSocket( SOCKET *sock )
{
	if ( *sock != INVALID_SOCKET )
	{
		shutdown( *sock, SD_BOTH );
		closesocket( *sock );
		*sock = INVALID_SOCKET;
	}
}


class CServerSocket
{
private:
	SOCKET m_Socket;
	SOCKET m_ServerSocket;

	CMessagePool m_MessagePool;

	char *m_pRecvBufPtr;
	char m_pRecvBuf[ SQDBG_NET_BUF_SIZE ];

	bool m_bWSAInit;

public:
	const char *m_pszLastMsgFmt;
	const char *m_pszLastMsg;

public:
	bool IsListening()
	{
		return m_ServerSocket != INVALID_SOCKET;
	}

	bool IsClientConnected()
	{
		return m_Socket != INVALID_SOCKET;
	}

	unsigned short GetServerPort()
	{
		if ( m_ServerSocket != INVALID_SOCKET )
		{
			sockaddr_in addr;
			socklen_t len = sizeof(addr);

			if ( getsockname( m_ServerSocket, (sockaddr*)&addr, &len ) != SOCKET_ERROR )
				return ntohs( addr.sin_port );
		}

		return 0;
	}

	bool ListenSocket( unsigned short port )
	{
		if ( m_ServerSocket != INVALID_SOCKET )
			return true;

#ifdef _WIN32
		if ( !m_bWSAInit )
		{
			WSADATA wsadata;
			if ( WSAStartup( MAKEWORD(2,2), &wsadata ) != 0 )
			{
				int err = errno;
				m_pszLastMsgFmt = "(sqdbg) WSA startup failed";
				m_pszLastMsg = strerr(err);
				return false;
			}
			m_bWSAInit = true;
		}
#endif

		m_ServerSocket = socket( AF_INET, SOCK_STREAM, 0 );

		if ( m_ServerSocket == INVALID_SOCKET )
		{
			int err = errno;
			Shutdown();
			m_pszLastMsgFmt = "(sqdbg) Failed to open socket";
			m_pszLastMsg = strerr(err);
			return false;
		}

		u_long iMode = 1;
#ifdef _WIN32
		if ( ioctlsocket( m_ServerSocket, FIONBIO, &iMode ) == SOCKET_ERROR )
#else
		int f = fcntl( m_ServerSocket, F_GETFL );
		if ( f == -1 || fcntl( m_ServerSocket, F_SETFL, f | O_NONBLOCK ) == -1 )
#endif
		{
			int err = errno;
			Shutdown();
			m_pszLastMsgFmt = "(sqdbg) Failed to set socket non-blocking";
			m_pszLastMsg = strerr(err);
			return false;
		}

		iMode = 1;

		if ( setsockopt( m_ServerSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&iMode, sizeof(iMode) ) == SOCKET_ERROR )
		{
			int err = errno;
			Shutdown();
			m_pszLastMsgFmt = "(sqdbg) Failed to set TCP nodelay";
			m_pszLastMsg = strerr(err);
			return false;
		}

		linger ln;
		ln.l_onoff = 0;
		ln.l_linger = 0;

		if ( setsockopt( m_ServerSocket, SOL_SOCKET, SO_LINGER, (char*)&ln, sizeof(ln) ) == SOCKET_ERROR )
		{
			int err = errno;
			Shutdown();
			m_pszLastMsgFmt = "(sqdbg) Failed to set don't linger";
			m_pszLastMsg = strerr(err);
			return false;
		}

		sockaddr_in addr;
		memset( &addr, 0, sizeof(addr) );
		addr.sin_family = AF_INET;
		addr.sin_port = htons( port );
		addr.sin_addr.s_addr = htonl( INADDR_ANY );

		if ( bind( m_ServerSocket, (sockaddr*)&addr, sizeof(addr) ) == SOCKET_ERROR )
		{
			int err = errno;
			Shutdown();
			m_pszLastMsgFmt = "(sqdbg) Failed to bind socket on port";
			m_pszLastMsg = strerr(err);
			return false;
		}

		if ( listen( m_ServerSocket, 0 ) == SOCKET_ERROR )
		{
			int err = errno;
			Shutdown();
			m_pszLastMsgFmt = "(sqdbg) Failed to listen to socket";
			m_pszLastMsg = strerr(err);
			return false;
		}

		return true;
	}

	bool Listen()
	{
		if ( m_ServerSocket == INVALID_SOCKET )
			return false;

		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		fd_set rfds;
		FD_ZERO( &rfds );
		FD_SET( m_ServerSocket, &rfds );

		select( 0, &rfds, NULL, NULL, &tv );

		if ( !FD_ISSET( m_ServerSocket, &rfds ) )
			return false;

		FD_CLR( m_ServerSocket, &rfds );

		sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);

		m_Socket = accept( m_ServerSocket, (sockaddr*)&addr, &addrlen );

		if ( m_Socket == INVALID_SOCKET )
			return false;

#ifndef _WIN32
		int f = fcntl( m_Socket, F_GETFL );
		if ( f == -1 || fcntl( m_Socket, F_SETFL, f | O_NONBLOCK ) == -1 )
		{
			int err = errno;
			DisconnectClient();
			m_pszLastMsgFmt = "(sqdbg) Failed to set socket non-blocking";
			m_pszLastMsg = strerr(err);
			return false;
		}
#endif

		m_pszLastMsg = inet_ntoa( addr.sin_addr );
		return true;
	}

	void Shutdown()
	{
		CloseSocket( &m_Socket );
		CloseSocket( &m_ServerSocket );

#ifdef _WIN32
		if ( m_bWSAInit )
		{
			WSACleanup();
			m_bWSAInit = false;
		}
#endif

		m_MessagePool.Clear();
		m_pRecvBufPtr = m_pRecvBuf;
		memset( m_pRecvBuf, -1, sizeof( m_pRecvBuf ) );
	}

	void DisconnectClient()
	{
		CloseSocket( &m_Socket );

		m_MessagePool.Clear();
		m_pRecvBufPtr = m_pRecvBuf;
		memset( m_pRecvBuf, -1, sizeof( m_pRecvBuf ) );
	}

	bool Send( const char *buf, int len )
	{
		for (;;)
		{
			int bytesSend = send( m_Socket, buf, len, 0 );

			if ( bytesSend == SOCKET_ERROR )
			{
				// Keep blocking
				if ( SocketWouldBlock() )
					continue;

				int err = errno;
				DisconnectClient();
				m_pszLastMsgFmt = "(sqdbg) Network error";
				m_pszLastMsg = strerr(err);
				return false;
			}

			if ( len == bytesSend )
				return true;

			len -= bytesSend;
		}
	}

	bool Recv()
	{
		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		fd_set rfds;
		FD_ZERO( &rfds );
		FD_SET( m_Socket, &rfds );

		select( 0, &rfds, NULL, NULL, &tv );

		if ( !FD_ISSET( m_Socket, &rfds ) )
			return true;

		FD_CLR( m_Socket, &rfds );

		u_long readlen = 0;
		ioctlsocket( m_Socket, FIONREAD, &readlen );

		int bufsize = m_pRecvBuf + sizeof(m_pRecvBuf) - m_pRecvBufPtr;

		if ( bufsize <= 0 || (unsigned int)bufsize < readlen )
		{
			DisconnectClient();
			m_pszLastMsgFmt = "(sqdbg) Net message buffer is full";
			m_pszLastMsg = NULL;
			return false;
		}

		for (;;)
		{
			int bytesRecv = recv( m_Socket, m_pRecvBufPtr, bufsize, 0 );

			if ( bytesRecv == SOCKET_ERROR )
			{
				if ( SocketWouldBlock() )
					break;

				int err = errno;
				DisconnectClient();
				m_pszLastMsgFmt = "(sqdbg) Network error";
				m_pszLastMsg = strerr(err);
				return false;
			}

			if ( !bytesRecv )
			{
#ifdef _WIN32
				WSASetLastError( WSAECONNRESET );
#else
				errno = ECONNRESET;
#endif
				int err = errno;
				DisconnectClient();
				m_pszLastMsgFmt = "(sqdbg) Client disconnected";
				m_pszLastMsg = strerr(err);
				return false;
			}

			m_pRecvBufPtr += bytesRecv;
			bufsize -= bytesRecv;
		}

		return true;
	}

	//
	// Header reader sets message pointer to the content start
	//
	template < bool (readHeader)( char **ppMsg, int *pLength ) >
	bool Parse()
	{
		// Nothing to parse
		if ( m_pRecvBufPtr == m_pRecvBuf )
			return true;

		char *pMsg = m_pRecvBuf;
		int nLength = sizeof(m_pRecvBuf);

		while ( readHeader( &pMsg, &nLength ) )
		{
			char *pMsgEnd = pMsg + (unsigned int)nLength;

			if ( pMsgEnd >= m_pRecvBuf + sizeof(m_pRecvBuf) )
			{
				DisconnectClient();
				m_pszLastMsgFmt = "(sqdbg) Client disconnected";

				if ( nLength == -1 )
				{
					m_pszLastMsg = "malformed message";
				}
				else
				{
					m_pszLastMsg = "content is too large";
				}

				return false;
			}

			// Entire message wasn't received, wait for it
			if ( m_pRecvBufPtr < pMsgEnd )
				break;

			m_MessagePool.Add( pMsg, nLength );

			// Last message
			if ( m_pRecvBufPtr == pMsgEnd )
			{
				memset( m_pRecvBuf, 0, m_pRecvBufPtr - m_pRecvBuf );
				m_pRecvBufPtr = m_pRecvBuf;
				break;
			}

			// Next message
			int shift = m_pRecvBufPtr - pMsgEnd;
			memmove( m_pRecvBuf, pMsgEnd, shift );
			memset( m_pRecvBuf + shift, 0, m_pRecvBufPtr - ( m_pRecvBuf + shift ) );
			m_pRecvBufPtr = m_pRecvBuf + shift;
			pMsg = m_pRecvBuf;
			nLength = sizeof(m_pRecvBuf);
		}

		return true;
	}

	template < typename T, void (T::*callback)( char *ptr, int len ) >
	void Execute( T *ctx )
	{
		m_MessagePool.Service< T, callback >( ctx );

		if ( m_Socket == INVALID_SOCKET && m_MessagePool.m_ElemCount == 0 )
		{
			m_MessagePool.Shrink();
		}
	}

public:
	CServerSocket() :
		m_Socket( INVALID_SOCKET ),
		m_ServerSocket( INVALID_SOCKET ),
		m_pRecvBufPtr( m_pRecvBuf ),
		m_bWSAInit( false )
	{
		STATIC_ASSERT( sizeof(m_pRecvBuf) <= ( 1 << ( sizeof(CMessagePool::message_t::len) * 8 ) ) );
	}
};

#endif // SQDBG_NET_H
