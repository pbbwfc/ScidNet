//////////////////////////////////////////////////////////////////////
//
//  FILE:       mfile.cpp
//              MFile class methods
//
//  Part of:    Scid (Shane's Chess Information Database)
//  Version:    2.0
//
//  Notice:     Copyright (c) 2000  Shane Hudson.  All rights reserved.
//
//  Author:     Shane Hudson (sgh@users.sourceforge.net)
//
//////////////////////////////////////////////////////////////////////

#include "common.h"
#include "mfile.h"
#include "misc.h"

void MFile::Init()
{
    Handle = NULL;
    FileMode = FMODE_Both;
    Type = MFILE_MEMORY;
    Capacity = 0;
    Location = 0;
    Data = NULL;
    CurrentPtr = NULL;
    FileBuffer = NULL;
    FileName = NULL;
    MappedFile = NULL;
}

void MFile::Extend()
{
    ASSERT(Type == MFILE_MEMORY);

    // Double Capacity, starting with at least 8 Kb:
    uint oldCapacity = Capacity;
    Capacity += Capacity;
    if (Capacity < 8192)
    {
        Capacity = 8192;
    }
    byte *oldData = Data;
    Data = new byte[Capacity];
    if (oldData != NULL)
    {
        // Copy data to new array:
        for (uint i = 0; i < oldCapacity; i++)
        {
            Data[i] = oldData[i];
        }
        delete[] oldData;
    }
    CurrentPtr = &(Data[Location]);
}

void MFile::SetBufferSize(uint bufsize)
{
    if (Type != MFILE_REGULAR)
    {
        return;
    }
    if (FileBuffer != NULL)
    {
        return;
    }
    FileBuffer = new char[bufsize];

    setvbuf(Handle, FileBuffer, _IOFBF, bufsize);
}

errorT
MFile::Seek(uint position)
{
    if (Type == MFILE_MEMORY)
    {
        while (position >= Capacity)
        {
            Extend();
        }
        CurrentPtr = &(Data[position]);
        Location = position;
        return OK;
    }

    // We do not need to seek if FileMode is not FMODE_Both and we are
    // already at the position:
    if (FileMode != FMODE_Both && Location == position)
    {
        return OK;
    }

    int result;
    if (Type == MFILE_MMAP)
    {
        result = 0; // always succeeding
    }
    else
    {
        result = fseek(Handle, position, 0);
    }
    if (result != 0)
    {
        return ERROR_FileSeek;
    }
    Location = position;
    return OK;
}

errorT
MFile::Flush()
{
    if (Type != MFILE_REGULAR || FileMode == FMODE_ReadOnly)
    {
        return OK;
    }
    return (fflush(Handle) == 0 ? OK : ERROR_FileWrite);
}

errorT
MFile::Open(const char *name, fileModeT fmode)
{
    ASSERT(Handle == NULL);
    const char *modeStr = NULL;
    switch (fmode)
    {
    case FMODE_ReadOnly:
        modeStr = "rb";
        break;
    case FMODE_WriteOnly:
        modeStr = "wb";
        break;
    case FMODE_Both:
        modeStr = "r+b";
        break;
    default:
        return ERROR_FileMode;
    }

    Handle = fopen(name, modeStr);
    if (Handle == NULL)
    {
        return ERROR_FileOpen;
    }
    Type = MFILE_REGULAR;

    FileMode = fmode;
    FileName = strDuplicate(name);
    Location = 0;
    return OK;
}

errorT
MFile::OpenMappedFile(const char *name, fileModeT fmode)
{
    ASSERT(Handle == NULL);
    ASSERT(fmode == FMODE_ReadOnly);

    Type = MFILE_MMAP;
    FileName = strDuplicate(name);
    MappedFile = new WinMMap(name);
    FileMode = fmode;
    Location = 0;

    return MappedFile->isOpen() ? OK : ERROR_FileOpen;
}

errorT
MFile::Create(const char *name, fileModeT fmode)
{
    ASSERT(Handle == NULL);
    const char *modeStr = NULL;
    switch (fmode)
    {
    case FMODE_WriteOnly:
        modeStr = "wb";
        break;
    case FMODE_Both:
        modeStr = "w+b";
        break;
    default:
        return ERROR_FileMode;
    }

    if ((Handle = fopen(name, modeStr)) == NULL)
    {
        return ERROR_FileOpen;
    }
    FileMode = fmode;
    FileName = strDuplicate(name);
    Location = 0;
    Type = MFILE_REGULAR;
    return OK;
}

errorT
MFile::Close()
{
    if (Type == MFILE_MEMORY)
    {
        if (Data != NULL)
        {
            delete[] Data;
        }
        Init();
        return OK;
    }
    int result = 0;
    if (Type == MFILE_MMAP)
    {
        delete MappedFile;
        MappedFile = NULL;
    }
    else
    {
        result = fclose(Handle);
    }

    if (FileBuffer != NULL)
    {
        delete[] FileBuffer;
        FileBuffer = NULL;
    }
    if (FileName != NULL)
    {
        delete[] FileName;
        FileName = NULL;
    }
    Init();
    return (result == 0 ? OK : ERROR);
}

errorT
MFile::WriteNBytes(const char *str, uint length)
{
    ASSERT(FileMode != FMODE_ReadOnly);

    if (Type == MFILE_MEMORY)
    {
        while (length-- > 0)
        {
            if (Location >= Capacity)
            {
                Extend();
            }
            *CurrentPtr++ = *str;
            Location++;
            str++;
        }
        return OK;
    }

    Location += length;

    return (fwrite(str, length, 1, Handle) != 1) ? ERROR_FileWrite : OK;
}

errorT
MFile::ReadNBytes(char *str, uint length)
{
    ASSERT(FileMode != FMODE_WriteOnly);
    if (Type != MFILE_REGULAR)
    {
        while (length-- > 0)
        {
            *str++ = ReadOneByte();
        }
    }
    else
    {
        // Optimization: if FREAD_OPTIMIZE is set,
        // We read the bytes with fread instead of a getc() loop.
        // This makes tree/material/etc searches go faster.

#define FREAD_OPTIMIZE
        Location += fread(str, 1, length, Handle);
    }
    return OK;
}

errorT
MFile::ReadLine(char *str, uint maxLength)
{
    ASSERT(FileMode != FMODE_WriteOnly);
    if (Type != MFILE_REGULAR)
    {
        while (1)
        {
            if (maxLength == 0)
            {
                break;
            }
            maxLength--;
            char ch = ReadOneByte();
            *str++ = ch;
            if (ch == '\n')
            {
                break;
            }
        }
        *str = 0;
    }
    else
    {
        fgets(str, (int)maxLength, Handle);
        Location = ftell(Handle);
    }
    return OK;
}

errorT
MFile::ReadLine(DString *dstr)
{
    int ch = ReadOneByte();
    while (ch != '\n' && ch != EOF)
    {
        if (ch != '\r')
        {
            dstr->AddChar(ch);
        }
        ch = ReadOneByte();
    }
    return OK;
}

errorT
MFile::WriteTwoBytes(uint value)
{
    ASSERT(FileMode != FMODE_ReadOnly);
    WriteOneByte((value >> 8) & 255);
    return WriteOneByte(value & 255);
}

uint MFile::ReadTwoBytes()
{
    uint result = 0;
    ASSERT(FileMode != FMODE_WriteOnly);
    result = ReadOneByte();
    result <<= 8;
    result += ReadOneByte();
    return result;
}

errorT
MFile::WriteThreeBytes(uint value)
{
    ASSERT(FileMode != FMODE_ReadOnly);
    WriteOneByte((value >> 16) & 255);
    WriteOneByte((value >> 8) & 255);
    return WriteOneByte(value & 255);
}

uint MFile::ReadThreeBytes()
{
    uint result = 0;
    ASSERT(FileMode != FMODE_WriteOnly);
    result = ReadOneByte();
    result <<= 8;
    result += ReadOneByte();
    result <<= 8;
    result += ReadOneByte();
    return result;
}

errorT
MFile::WriteFourBytes(uint value)
{
    ASSERT(FileMode != FMODE_ReadOnly);
    WriteOneByte((value >> 24) & 255);
    WriteOneByte((value >> 16) & 255);
    WriteOneByte((value >> 8) & 255);
    return WriteOneByte(value & 255);
}

uint MFile::ReadFourBytes()
{
    uint result = 0;
    ASSERT(FileMode != FMODE_WriteOnly);
    result = ReadOneByte();
    result <<= 8;
    result += ReadOneByte();
    result <<= 8;
    result += ReadOneByte();
    result <<= 8;
    result += ReadOneByte();
    return result;
}

//////////////////////////////////////////////////////////////////////
//  End of file: mfile.cpp
//////////////////////////////////////////////////////////////////////
