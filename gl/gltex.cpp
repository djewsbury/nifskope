/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools projectmay not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#ifdef QT_OPENGL_LIB

#include <QtOpenGL>

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

#include "glscene.h"
#include "gltex.h"

#include <GL/glext.h>

QStringList TexCache::texfolders;

static PFNGLACTIVETEXTUREARBPROC       _glActiveTextureARB       = 0;
static PFNGLCLIENTACTIVETEXTUREARBPROC _glClientActiveTextureARB = 0;
static PFNGLCOMPRESSEDTEXIMAGE2DPROC   _glCompressedTexImage2D   = 0;

static int num_texture_units = 0;

void initializeTextureUnits( const QGLContext * context )
{
	QString extensions( (const char *) glGetString(GL_EXTENSIONS) );
	//foreach ( QString e, extensions.split( " " ) )
	//	qWarning() << e;
	/*
	if ( ! extensions.contains( "GL_ARB_texture_compression" ) )
		qWarning( "need OpenGL extension GL_ARB_texture_compression for DDS textures" );
	if ( ! extensions.contains( "GL_EXT_texture_compression_s3tc" ) )
		qWarning( "need OpenGL extension GL_EXT_texture_compression_s3tc for DDS textures" );
	*/
	_glCompressedTexImage2D = (PFNGLCOMPRESSEDTEXIMAGE2DPROC) context->getProcAddress( "glCompressedTexImage2D" );
	if ( ! _glCompressedTexImage2D )
		_glCompressedTexImage2D = (PFNGLCOMPRESSEDTEXIMAGE2DPROC) context->getProcAddress( "glCompressedTexImage2DARB" );

	if ( ! _glCompressedTexImage2D )
		qWarning( "texture compression not supported" );
	
	_glActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC) context->getProcAddress( "glActiveTextureARB" );
	_glClientActiveTextureARB = (PFNGLCLIENTACTIVETEXTUREARBPROC) context->getProcAddress( "glClientActiveTextureARB" );
	
	if ( ! _glActiveTextureARB || ! _glClientActiveTextureARB )
	{
		qWarning( "multitexturing not supported" );
		num_texture_units = 1;
	}
	else
	{
		glGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &num_texture_units );
		if ( num_texture_units < 1 )
			num_texture_units = 1;
		//qWarning() << "texture units" << num_texture_units;
	}
}

bool isPowerOfTwo( unsigned int x )
{
	while ( ! ( x == 0 || x & 1 ) )
		x = x >> 1;
	return ( x == 1 );
}

int generateMipMaps( int m )
{
	GLint w = 0, h = 0;
	
	glGetTexLevelParameteriv( GL_TEXTURE_2D, m-1, GL_TEXTURE_WIDTH, &w );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, m-1, GL_TEXTURE_HEIGHT, &h );
	
	//qWarning() << m-1 << w << h;

	quint8 * data = (quint8 *) malloc( w * h * 4 );
	glGetTexImage( GL_TEXTURE_2D, m-1, GL_RGBA, GL_UNSIGNED_BYTE, data );
	
	while ( w > 1 || h > 1 )
	{
		const quint8 * src = data;
		quint8 * dst = data;
		
		quint32 xo = ( w > 1 ? 1*4 : 0 );
		quint32 yo = ( h > 1 ? w*4 : 0 );
		
		w /= 2;
		h /= 2;
		
		if ( w == 0 ) w = 1;
		if ( h == 0 ) h = 1;
		
		//qWarning() << m << w << h;
		
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				for ( int b = 0; b < 4; b++ )
				{
					*dst++ = ( *(src+xo) + *(src+yo) + *(src+xo+yo) + *src++ ) / 4;
				}
				src += xo;
			}
			src += yo;
		}
		
		glTexImage2D( GL_TEXTURE_2D, m++, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
	}
	
	free( data );
	
	return m;
}

bool uncompressRLE( QIODevice & f, int w, int h, int bytespp, quint8 * pixel )
{
	QByteArray data = f.readAll();
	
	int c = 0;
	int o = 0;
	
	quint8 rl;
	while ( c < w * h )
	{
		rl = data[o++];
		if ( rl & 0x80 )
		{
			quint8 px[4];
			for ( int b = 0; b < bytespp; b++ )
				px[b] = data[o++];
			rl &= 0x7f;
			do
			{
				for ( int b = 0; b < bytespp; b++ )
					*pixel++ = px[b];
			}
			while ( ++c < w*h && rl-- > 0  );
		}
		else
		{
			do
			{
				for ( int b = 0; b < bytespp; b++ )
					*pixel++ = data[o++];
			}
			while (  ++c < w*h && rl-- > 0 );
		}
		if ( o >= data.count() ) return false;
	}
	return true;
}

void convertToRGBA( const quint8 * data, int w, int h, int bytespp, const quint32 mask[], bool flipV, bool flipH, quint8 * pixl )
{
	memset( pixl, 0, w * h * 4 );

	static const int rgbashift[4] = { 0, 8, 16, 24 };
	
	for ( int a = 0; a < 4; a++ )
	{
		if ( mask[a] )
		{
			quint32 msk = mask[ a ];
			int rshift = 0;
			while ( msk != 0 && ( msk & 0xffffff00 ) ) { msk = msk >> 1; rshift++; }
			int lshift = rgbashift[ a ];
			while ( msk != 0 && ( ( msk & 0x80 ) == 0 ) )	{ msk = msk << 1; lshift++; }
			msk = mask[ a ];
			
			const quint8 * src = data;
			const quint32 inc = ( flipH ? -1 : 1 );
			for ( int y = 0; y < h; y++ )
			{
				quint32 * dst = (quint32 *) ( pixl + 4 * ( w * ( flipV ? h - y - 1 : y ) + ( flipH ? w - 1 : 0 ) ) );
				if ( rshift == lshift )
				{
					for ( int x = 0; x < w; x++ )
					{
						*dst |= *( (const quint32 *) src ) & msk;
						dst += inc;
						src += bytespp;
					}
				}
				else
				{
					for ( int x = 0; x < w; x++ )
					{
						*dst |= ( *( (const quint32 *) src ) & msk ) >> rshift << lshift;
						dst += inc;
						src += bytespp;
					}
				}
			}
		}
		else if ( a == 3 )
		{
			quint32 * dst = (quint32 *) pixl;
			quint32 x = 0xff << rgbashift[ a ];
			for ( int c = w * h; c > 0; c-- )
				*dst++ |= x;
		}
	}
}

int texLoadRaw( QIODevice & f, int width, int height, int num_mipmaps, int bpp, int bytespp, const quint32 mask[], bool flipV = false, bool flipH = false, bool rle = false )
{
	if ( bytespp * 8 != bpp )
	{
		qWarning() << "texLoadRaw() : unsupported image depth" << bpp << "/" << bytespp;
		return 0;
	}
	
	if ( bpp > 32 || bpp < 8 )
	{	// check image depth
		qWarning() << "texLoadRaw() : unsupported image depth" << bpp;
		return 0;
	}
	
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glPixelStorei( GL_UNPACK_SWAP_BYTES, GL_FALSE );
	
	quint8 * data1 = (quint8 *) malloc( width * height * 4 );
	quint8 * data2 = (quint8 *) malloc( width * height * 4 );
	
	int w = width;
	int h = height;
	int m = 0;
	
	while ( m < num_mipmaps )
	{
		w = width >> m;
		h = height >> m;
		
		if ( w == 0 ) w = 1;
		if ( h == 0 ) h = 1;
		
		if ( rle )
		{
			if ( ! uncompressRLE( f, w, h, bytespp, data1 ) )
			{
				qWarning() << "texLoadRaw() : unexpected EOF";
				free( data2 );
				free( data1 );
				return ( m ? 1 : 0 );
			}
		}
		else if ( f.read( (char *) data1, w * h * bytespp ) != w * h * bytespp )
		{
			qWarning() << "texLoadRaw() : unexpected EOF";
			free( data2 );
			free( data1 );
			return ( m ? 1 : 0 );
		}
		
		convertToRGBA( data1, w, h, bytespp, mask, flipV, flipH, data2 );
		
		glTexImage2D( GL_TEXTURE_2D, m++, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data2 );
		
		if ( w == 1 && h == 1 )
			break;
	}
	
	free( data2 );
	free( data1 );
	
	if ( w > 1 || h > 1 )
		m = generateMipMaps( m );
	
	return m;
}

int texLoadPal( QIODevice & f, int width, int height, int num_mipmaps, int bpp, int bytespp, const quint32 colormap[], bool flipV, bool flipH, bool rle )
{
	if ( bpp != 8 || bytespp != 1 )
	{
		qWarning() << "texLoadPal() : unsupported image depth" << bpp << "/" << bytespp;
		return 0;
	}
	
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glPixelStorei( GL_UNPACK_SWAP_BYTES, GL_FALSE );
	
	quint8 * data = (quint8 *) malloc( width * height * 1 );
	quint8 * pixl = (quint8 *) malloc( width * height * 4 );
	
	int w = width;
	int h = height;
	int m = 0;
	
	while ( m < num_mipmaps )
	{
		w = width >> m;
		h = height >> m;
		
		if ( w == 0 ) w = 1;
		if ( h == 0 ) h = 1;
		
		if ( rle )
		{
			if ( ! uncompressRLE( f, w, h, bytespp, data ) )
			{
				qWarning() << "texLoadPal() : unexpected EOF";
				free( pixl );
				free( data );
				return ( m ? 1 : 0 );
			}
		}
		else if ( f.read( (char *) data, w * h * bytespp ) != w * h * bytespp )
		{
			qWarning() << "texLoadPal() : unexpected EOF";
			free( pixl );
			free( data );
			return ( m ? 1 : 0 );
		}
		
		quint8 * src = data;
		for ( int y = 0; y < h; y++ )
		{
			quint32 * dst = (quint32 *) ( pixl + 4 * ( w * ( flipV ? h - y - 1 : y ) + ( flipH ? w - 1 : 0 ) ) );
			for ( int x = 0; x < w; x++ )
			{
				*dst++ = colormap[*src++];
			}
		}
		
		glTexImage2D( GL_TEXTURE_2D, m++, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixl );
		
		if ( w == 1 && h == 1 )
			break;
	}
	
	free( pixl );
	free( data );
	
	if ( w > 1 || h > 1 )
		m = generateMipMaps( m );
	
	return m;
}

#define DDSD_MIPMAPCOUNT           0x00020000
#define DDPF_FOURCC                0x00000004

// DDS format structure
struct DDSFormat {
    quint32 dwSize;
    quint32 dwFlags;
    quint32 dwHeight;
    quint32 dwWidth;
    quint32 dwLinearSize;
    quint32 dummy1;
    quint32 dwMipMapCount;
    quint32 dummy2[11];
    struct {
	quint32 dwSize;
	quint32 dwFlags;
	quint32 dwFourCC;
	quint32 dwBPP;
	quint32 dwRMask;
	quint32 dwGMask;
	quint32 dwBMask;
	quint32 dwAMask;
    } ddsPixelFormat;
};

// compressed texture pixel formats
#define FOURCC_DXT1  0x31545844
#define FOURCC_DXT2  0x32545844
#define FOURCC_DXT3  0x33545844
#define FOURCC_DXT4  0x34545844
#define FOURCC_DXT5  0x35545844

// thanks nvidia for providing the source code to flip dxt images

typedef struct
{
	unsigned short col0, col1;
	unsigned char row[4];
} DXTColorBlock_t;

typedef struct
{
	unsigned short row[4];
} DXT3AlphaBlock_t;

typedef struct
{
	unsigned char alpha0, alpha1;
	unsigned char row[6];
} DXT5AlphaBlock_t;

void SwapMem(void *byte1, void *byte2, int size)
{
	unsigned char *tmp=(unsigned char *)malloc(sizeof(unsigned char)*size);
	memcpy(tmp, byte1, size);
	memcpy(byte1, byte2, size);
	memcpy(byte2, tmp, size);
	free(tmp);
}

inline void SwapChar( unsigned char * x, unsigned char * y )
{
	unsigned char z = *x;
	*x = *y;
	*y = z;
}

inline void SwapShort( unsigned short * x, unsigned short * y )
{
	unsigned short z = *x;
	*x = *y;
	*y = z;
}

void flipDXT1Blocks(DXTColorBlock_t *Block, int NumBlocks)
{
	int i;
	DXTColorBlock_t *ColorBlock=Block;
	for(i=0;i<NumBlocks;i++)
	{
		SwapChar( &ColorBlock->row[0], &ColorBlock->row[3] );
		SwapChar( &ColorBlock->row[1], &ColorBlock->row[2] );
		ColorBlock++;
	}
}

void flipDXT3Blocks(DXTColorBlock_t *Block, int NumBlocks)
{
	int i;
	DXTColorBlock_t *ColorBlock=Block;
	DXT3AlphaBlock_t *AlphaBlock;
	for(i=0;i<NumBlocks;i++)
	{
		AlphaBlock=(DXT3AlphaBlock_t *)ColorBlock;
		SwapShort( &AlphaBlock->row[0], &AlphaBlock->row[3] );
		SwapShort( &AlphaBlock->row[1], &AlphaBlock->row[2] );
		ColorBlock++;
		SwapChar( &ColorBlock->row[0], &ColorBlock->row[3] );
		SwapChar( &ColorBlock->row[1], &ColorBlock->row[2] );
		ColorBlock++;
	}
}

void flipDXT5Alpha(DXT5AlphaBlock_t *Block)
{
	unsigned long *Bits, Bits0=0, Bits1=0;

	memcpy(&Bits0, &Block->row[0], sizeof(unsigned char)*3);
	memcpy(&Bits1, &Block->row[3], sizeof(unsigned char)*3);

	Bits=((unsigned long *)&(Block->row[0]));
	*Bits&=0xff000000;
	*Bits|=(unsigned char)(Bits1>>12)&0x00000007;
	*Bits|=(unsigned char)((Bits1>>15)&0x00000007)<<3;
	*Bits|=(unsigned char)((Bits1>>18)&0x00000007)<<6;
	*Bits|=(unsigned char)((Bits1>>21)&0x00000007)<<9;
	*Bits|=(unsigned char)(Bits1&0x00000007)<<12;
	*Bits|=(unsigned char)((Bits1>>3)&0x00000007)<<15;
	*Bits|=(unsigned char)((Bits1>>6)&0x00000007)<<18;
	*Bits|=(unsigned char)((Bits1>>9)&0x00000007)<<21;

	Bits=((unsigned long *)&(Block->row[3]));
	*Bits&=0xff000000;
	*Bits|=(unsigned char)(Bits0>>12)&0x00000007;
	*Bits|=(unsigned char)((Bits0>>15)&0x00000007)<<3;
	*Bits|=(unsigned char)((Bits0>>18)&0x00000007)<<6;
	*Bits|=(unsigned char)((Bits0>>21)&0x00000007)<<9;
	*Bits|=(unsigned char)(Bits0&0x00000007)<<12;
	*Bits|=(unsigned char)((Bits0>>3)&0x00000007)<<15;
	*Bits|=(unsigned char)((Bits0>>6)&0x00000007)<<18;
	*Bits|=(unsigned char)((Bits0>>9)&0x00000007)<<21;
}

void flipDXT5Blocks(DXTColorBlock_t *Block, int NumBlocks)
{
	DXTColorBlock_t *ColorBlock=Block;
	DXT5AlphaBlock_t *AlphaBlock;
	int i;

	for(i=0;i<NumBlocks;i++)
	{
		AlphaBlock=(DXT5AlphaBlock_t *)ColorBlock;

		flipDXT5Alpha(AlphaBlock);
		ColorBlock++;

		SwapChar( &ColorBlock->row[0], &ColorBlock->row[3] );
		SwapChar( &ColorBlock->row[1], &ColorBlock->row[2] );
		ColorBlock++;
	}
}

void flipDXT( GLenum glFormat, int width, int height, unsigned char * image )
{
	int linesize, j;

	DXTColorBlock_t *top;
	DXTColorBlock_t *bottom;
	int xblocks=width/4;
	int yblocks=height/4;

	switch ( glFormat)
	{
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: 
			linesize=xblocks*8;
			for(j=0;j<(yblocks>>1);j++)
			{
				top=(DXTColorBlock_t *)(image+j*linesize);
				bottom=(DXTColorBlock_t *)(image+(((yblocks-j)-1)*linesize));
				flipDXT1Blocks(top, xblocks);
				flipDXT1Blocks(bottom, xblocks);
				SwapMem(bottom, top, linesize);
			}
			break;

		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			linesize=xblocks*16;
			for(j=0;j<(yblocks>>1);j++)
			{
				top=(DXTColorBlock_t *)(image+j*linesize);
				bottom=(DXTColorBlock_t *)(image+(((yblocks-j)-1)*linesize));
				flipDXT3Blocks(top, xblocks);
				flipDXT3Blocks(bottom, xblocks);
				SwapMem(bottom, top, linesize);
			}
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			linesize=xblocks*16;
			for(j=0;j<(yblocks>>1);j++)
			{
				top=(DXTColorBlock_t *)(image+j*linesize);
				bottom=(DXTColorBlock_t *)(image+(((yblocks-j)-1)*linesize));
				flipDXT5Blocks(top, xblocks);
				flipDXT5Blocks(bottom, xblocks);
				SwapMem(bottom, top, linesize);
			}
			break;
		default:
			return;
	}
}


int texLoadDXT( QFile & f, quint32 compression, quint32 width, quint32 height, quint32 mipmaps, bool flipV = false )
{
	int blockSize = 8;
	GLenum glFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
	
	switch( compression )
	{
		case FOURCC_DXT1:
			glFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
			blockSize = 8;
			break;
		case FOURCC_DXT3:
			glFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
			blockSize = 16;
			break;
		case FOURCC_DXT5:
			glFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			blockSize = 16;
			break;
		default:
			qWarning() << "texLoadDXT(" << f.fileName() << ") : unsupported DXT compression type";
			return 0;
	}
	
	if ( !_glCompressedTexImage2D )
	{
		qDebug() << "texLoadDXT(" << f.fileName() << ") : DXT image compression not supported by your open gl implementation";
		return 0;
	}
	
	GLubyte * pixels = (GLubyte *) malloc( ( ( width + 3 ) / 4 ) * ( ( height + 3 ) / 4 ) * blockSize );
	unsigned int w = width, h = height, s;
	unsigned int m = 0;
	
	while ( m < mipmaps )
	{
		w = width >> m;
		h = height >> m;
		
		if ( w == 0 ) w = 1;
		if ( h == 0 ) h = 1;
		
		s = ((w+3)/4) * ((h+3)/4) * blockSize;
		
		if ( f.read( (char *) pixels, s ) != s )
		{
			qWarning() << "texLoadDXT(" << f.fileName() << ") : unexpected EOF";
			free( pixels );
			return ( m ? 1 : 0 );
		}
		
		if ( flipV )
			flipDXT( glFormat, w, h, pixels );
		
		_glCompressedTexImage2D( GL_TEXTURE_2D, m++, glFormat, w, h, 0, s, pixels );
		
		if ( w == 1 && h == 1 )
			break;
	}
	
	if ( w > 1 || h > 1 )
		return 1;
	else
		return m;
}

/*
 *  load a (compressed) dds texture
 */
 
int texLoadDDS( const QString & filename )
{
	QFile f( filename );
	if ( ! f.open( QIODevice::ReadOnly ) )
	{
		qWarning() << "texLoadDDS(" << filename << ") : could not open file";
		return 0;
	}
	
	char tag[4];
	f.read(&tag[0], 4);
	DDSFormat ddsHeader;

	if ( strncmp( tag,"DDS ", 4 ) != 0 || f.read((char *) &ddsHeader, sizeof(DDSFormat)) != sizeof( DDSFormat ) )
	{
		qWarning() << "texLoadDDS(" << filename << ") : not a DDS file";
		return 0;
	}
	
	if ( !( ddsHeader.dwFlags & DDSD_MIPMAPCOUNT ) )
		ddsHeader.dwMipMapCount = 1;
	
	if ( ! ( isPowerOfTwo( ddsHeader.dwWidth ) && isPowerOfTwo( ddsHeader.dwHeight ) ) )
	{
		qWarning() << "texLoadDDS(" << filename << ") : image dimensions must be power of two";
		return 0;
	}

	f.seek(ddsHeader.dwSize + 4);
	
	if ( ddsHeader.ddsPixelFormat.dwFlags & DDPF_FOURCC )
	{
		return texLoadDXT( f, ddsHeader.ddsPixelFormat.dwFourCC, ddsHeader.dwWidth, ddsHeader.dwHeight, ddsHeader.dwMipMapCount );
	}
	else
	{
		return texLoadRaw( f, ddsHeader.dwWidth, ddsHeader.dwHeight,
			ddsHeader.dwMipMapCount, ddsHeader.ddsPixelFormat.dwBPP, ddsHeader.ddsPixelFormat.dwBPP / 8,
			&ddsHeader.ddsPixelFormat.dwRMask );
	}
}

/*
 *  load a tga texture
 */

#define TGA_COLORMAP	1
#define TGA_COLOR		2
#define TGA_GREY		3
#define TGA_COLORMAP_RLE 9
#define TGA_COLOR_RLE	10
#define TGA_GREY_RLE	11

int texLoadTGA( const QString & filename )
{
	QFile f( filename );
	if ( ! f.open( QIODevice::ReadOnly ) )
	{
		qWarning() << "texLoadTGA(" << filename << ") : could not open file";
		return 0;
	}
	
	// read in tga header
	quint8 hdr[18];
	qint64 readBytes = f.read((char *)hdr, 18);
	if ( readBytes != 18 )
	{
		qWarning() << "texLoadTGA(" << filename << ") : unexpected EOF";
		return 0;
	}
	if ( hdr[0] ) f.read( hdr[0] );
	
	quint8 depth = hdr[16];
	//quint8 alphaDepth  = hdr[17] & 15;
	bool flipV = ! ( hdr[17] & 32 );
	bool flipH = hdr[17] & 16;
	quint16 width = hdr[12] + 256 * hdr[13];
	quint16 height = hdr[14] + 256 * hdr[15];
	
	if ( ! ( isPowerOfTwo( width ) && isPowerOfTwo( height ) ) )
	{
		qWarning() << "texLoadTGA(" << filename << ") : image dimensions must be power of two";
		return 0;
	}
	
	quint32 colormap[256];
	
	if ( hdr[1] )
	{	// color map present
		quint16 offset = hdr[3] + 256 * hdr[4];
		quint16 length = hdr[5] + 256 * hdr[6];
		quint8 bits = hdr[7];
		quint8 bytes = bits / 8;
		
		//qWarning() << "COLORMAP" << "offset" << offset << "length" << length << "bits" << bits << "depth" << depth;
		
		if ( bits != 32 && bits != 24 )
		{
			qWarning() << "texLoadTGA(" << filename << ") : image sub format not supported" << hdr[2] << depth << bits;
			return 0;
		}
		
		quint32 cnt = offset;
		quint32 col;
		while ( length-- )
		{
			col = 0;
			if ( f.read( (char *) &col, bytes ) != bytes )
			{
				qWarning() << "texLoadTGA(" << filename << ") : unexpected EOF";
				return 0;
			}
			
			if ( cnt < 256 )
			{
				switch ( bits )
				{
					case 24:
						colormap[cnt] = ( ( col & 0x00ff0000 ) >> 16 ) | ( col & 0x0000ff00 ) | ( ( col & 0xff ) << 16 ) | 0xff000000;
						break;
					case 32:
						colormap[cnt] = ( ( col & 0x00ff0000 ) >> 16 ) | ( col & 0xff00ff00 ) | ( ( col & 0xff ) << 16 );
						break;
				}
			}
			cnt++;
		}
	}

	// check format and call texLoadPal / texLoadRaw
	switch( hdr[2] )
	{
	case TGA_COLORMAP:
	case TGA_COLORMAP_RLE:
		if ( depth == 8 && hdr[1] )
		{
			return texLoadPal( f, width, height, 1, depth, depth/8, colormap, flipV, flipH, hdr[2] == TGA_COLORMAP_RLE );
		}
		break;
	case TGA_GREY:
	case TGA_GREY_RLE:
		if ( depth == 8 )
		{
			static const quint32 TGA_L_MASK[4] = { 0xff, 0xff, 0xff, 0x00 };
			return texLoadRaw( f, width, height, 1, 8, 1, TGA_L_MASK, flipV, flipH, hdr[2] == TGA_GREY_RLE );
		}
		else if ( depth == 16 )
		{
			static const quint32 TGA_LA_MASK[4] = { 0x00ff, 0x00ff, 0x00ff, 0xff00 };
			return texLoadRaw( f, width, height, 1, 16, 2, TGA_LA_MASK, flipV, flipH, hdr[2] == TGA_GREY_RLE );
		}
		break;
	case TGA_COLOR:
	case TGA_COLOR_RLE:
		if ( depth == 32 )
		{
			static const quint32 TGA_RGBA_MASK[4] = { 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000 };
			return texLoadRaw( f, width, height, 1, 32, 4, TGA_RGBA_MASK, flipV, flipH, hdr[2] == TGA_COLOR_RLE );
		}
		else if ( depth == 24 )
		{
			static const quint32 TGA_RGB_MASK[4] = { 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000 };
			return texLoadRaw( f, width, height, 1, 24, 3, TGA_RGB_MASK, flipV, flipH, hdr[2] == TGA_COLOR_RLE );
		}
		break;
	}
	qWarning() << "texLoadTGA(" << filename << ") : image sub format not supported" << hdr[2] << depth;
	return 0;
}

quint32 get32( quint8 * x )
{
	return *( (quint32 *) x );
}

quint16 get16( quint8 * x )
{
	return *( (quint16 *) x );
}

int texLoadBMP( const QString & filename )
{
	QFile f( filename );
	if ( ! f.open( QIODevice::ReadOnly ) )
	{
		qWarning() << "texLoadBMP(" << filename << ") : could not open file";
		return 0;
	}
	
	// read in bmp header
	quint8 hdr[54];
	qint64 readBytes = f.read((char *)hdr, 54);
	
	if ( readBytes != 54 || strncmp((char*)hdr,"BM", 2) != 0)
	{
		qWarning() << "texLoadBMP(" << filename << ") : not a BMP file";
		return 0;
	}
	
	unsigned int width = get32( &hdr[18] );
	unsigned int height = get32( &hdr[22] );
	unsigned int bpp = get16( &hdr[28] );
	unsigned int compression = get32( &hdr[30] );
	unsigned int offset = get32( &hdr[10] );
	
	f.seek( offset );
	
	if ( ! ( isPowerOfTwo( width ) && isPowerOfTwo( height ) ) )
	{
		qWarning() << "texLoadBMP(" << filename << ") : image dimensions must be power of two";
		return 0;
	}

	switch ( compression )
	{
		case 0:
			if ( bpp == 24 )
			{
				static const quint32 BMP_RGBA_MASK[4] = { 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000 };
				return texLoadRaw( f, width, height, 1, bpp, 3, BMP_RGBA_MASK, true );
			}
			break;
		case FOURCC_DXT5:
		case FOURCC_DXT3:
		case FOURCC_DXT1:
			return texLoadDXT( f, compression, width, height, 1, true );
	}

	qWarning() << "texLoadBMP(" << filename << ") : image sub format not supported";
	/*
	qWarning( "size %i,%i", width, height );
	qWarning( "plns %i", get16( &hdr[26] ) );
	qWarning( "bpp  %i", bpp );
	qWarning( "cmpr %08x", compression );
	qWarning( "ofs  %i", offset );
	*/
	return 0;
}



int TexCache::bind( const QString & fname )
{
	Tex * tx = textures.value( fname );
	if ( ! tx )
	{
		tx = new Tex;
		tx->filename = fname;
		tx->id = 0;
		tx->mipmaps = 0;
		
		textures.insert( tx->filename, tx );
	}
	
	if ( tx->filepath.isEmpty() || ! QFile::exists( tx->filepath ) )
		tx->filepath = find( tx->filename, nifFolder );
	
	if ( ! tx->id || ( QFile::exists( tx->filepath ) && tx->loaded.secsTo( QFileInfo( tx->filepath ).lastModified() ) > 2 ) )
		load( tx );
	
	glBindTexture( GL_TEXTURE_2D, tx->id );
	
	tx->used = true;
	return tx->mipmaps;
}

void TexCache::load( Tex * tx )
{
	if ( ! tx->id )
		glGenTextures( 1, &tx->id );
	tx->mipmaps = 0;
	
	glBindTexture( GL_TEXTURE_2D, tx->id );
	
	if ( ! QFile::exists( tx->filepath ) )
		return;
	
	if ( tx->filepath.endsWith( ".dds", Qt::CaseInsensitive ) )
		tx->mipmaps = texLoadDDS( tx->filepath );
	else if ( tx->filepath.endsWith( ".tga", Qt::CaseInsensitive ) )
		tx->mipmaps = texLoadTGA( tx->filepath );
	else if ( tx->filepath.endsWith( ".bmp", Qt::CaseInsensitive ) )
		tx->mipmaps = texLoadBMP( tx->filepath );
	
	tx->loaded = QDateTime::currentDateTime();
}

void TexCache::flush()
{
	foreach ( Tex * tx, textures )
	{
		if ( tx->id )
			glDeleteTextures( 1, &tx->id );
	}
	qDeleteAll( textures );
	textures.clear();
}

void TexCache::purgeUnused()
{
	QMutableHashIterator<QString, Tex *> it( textures );
	while ( it.hasNext() )
	{
		it.next();
		if ( !it.value()->used )
		{
			Tex * tx = it.value();
			it.remove();
			if ( tx->id )
				glDeleteTextures( 1, &tx->id );
			delete tx;
		}
	}
}

void TexCache::clearUsedState()
{
	foreach ( Tex * tx, textures )
		tx->used = false;
}

QString TexCache::find( const QString & file, const QString & additionalFolders )
{
	if ( file.isEmpty() )
		return QString();
	
	QString filename = file.toLower();
	
	while ( filename.startsWith( "/" ) or filename.startsWith( "\\" ) )
		filename.remove( 0, 1 );
	
	QStringList extensions;
	extensions << ".tga" << ".dds" << ".bmp";
	bool replaceExt = false;
	foreach ( QString ext, extensions )
		if ( filename.endsWith( ext ) )
		{
			extensions.removeAll( ext );
			extensions.prepend( ext );
			filename = filename.left( filename.length() - ext.length() );
			replaceExt = true;
			break;
		}
	
	// attempt to find the texture in one of the folders
	QDir dir;
	foreach ( QString ext, extensions )
	{
		if ( replaceExt )
			filename += ext;
		foreach ( QString folder, texfolders + additionalFolders.split( ";" ) )
		{
			dir.setPath( folder );
			if ( dir.exists( filename ) )
				return dir.filePath( filename );
		}
		if ( replaceExt )
			filename = filename.left( filename.length() - ext.length() );
		else
			break;
	}
	
	if ( replaceExt )
		return filename + extensions.value( 0 );
	else
		return filename;
}

bool TexturingProperty::bind( int id, const QString & fname )
{
	GLuint mipmaps = 0;
	if ( id >= 0 && id <= 7 && ( mipmaps = scene->bindTexture( fname.isEmpty() ? fileName( id ) : fname ) ) )
	{
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textures[id].wrapS );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textures[id].wrapT );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		if ( textures[id].hasTransform )
		{
			glTranslatef( - textures[id].center[0], - textures[id].center[1], 0 );
			glRotatef( textures[id].rotation, 0, 0, 1 );
			glTranslatef( textures[id].center[0], textures[id].center[1], 0 );
			glScalef( textures[id].tiling[0], textures[id].tiling[1], 1 );
			glTranslatef( textures[id].translation[0], textures[id].translation[1], 0 );
		}
		glMatrixMode( GL_MODELVIEW );
		return true;
	}
	else
		return false;
}

bool checkSet( int s, const QList< QVector< Vector2 > > & texcoords )
{
	return s >= 0 && s < texcoords.count() && texcoords[s].count();
}

bool TexturingProperty::bind( int id, const QList< QVector< Vector2 > > & texcoords )
{
	if ( checkSet( textures[id].coordset, texcoords ) && bind( id ) )
	{
		glEnable( GL_TEXTURE_2D );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, 0, texcoords[ textures[id].coordset ].data() );
		return true;
	}
	else
	{
		glDisable( GL_TEXTURE_2D );
		return false;
	}
}

bool TexturingProperty::bind( int id, const QList< QVector< Vector2 > > & texcoords, int stage )
{
	return ( activateTextureUnit( stage ) && bind( id, texcoords ) );
}

bool activateTextureUnit( int stage )
{
	if ( num_texture_units <= 1 )
		return ( stage == 0 );
	
	if ( stage < num_texture_units )
	{
		_glActiveTextureARB( GL_TEXTURE0_ARB + stage );
		_glClientActiveTextureARB( GL_TEXTURE0_ARB + stage );	
		return true;
	}
	else
		return false;
}

void resetTextureUnits()
{
	if ( num_texture_units <= 1 )
	{
		glDisable( GL_TEXTURE_2D );
		return;
	}
	
	for ( int x = num_texture_units-1; x >= 0; x-- )
	{
		_glActiveTextureARB( GL_TEXTURE0_ARB + x );
		glDisable( GL_TEXTURE_2D );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		_glClientActiveTextureARB( GL_TEXTURE0_ARB + x );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}
}

#endif