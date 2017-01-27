/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2009-2014 Klaus Post
    Copyright (C) 2014 Pedro Côrte-Real

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "decoders/ThreefrDecoder.h"
#include "common/Common.h" // for ushort16, uint32, int32
#include "common/Point.h"  // for iPoint2D
#include "decoders/RawDecoderException.h" // for ThrowRDE, RawDecoderException
#include "decompressors/HasselbladDecompressor.h"
#include "decompressors/LJpegPlain.h"
#include "io/BitPumpMSB32.h"         // for BitPumpMSB32
#include "io/ByteStream.h"           // for ByteStream
#include "metadata/CameraMetaData.h" // for CameraMetaData
#include "tiff/TiffEntry.h"          // for TiffEntry
#include "tiff/TiffIFD.h"            // for TiffIFD
#include "tiff/TiffTag.h"            // for ::STRIPOFFSETS, ::MODEL
#include <algorithm>                 // for max
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio> // for NULL
#include <cstdlib>
#include <cstring>
#include <iostream> // for ostringstream, operator<<
#include <list>
#include <map> // for map, _Rb_tree_iterator
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>  // for string, operator==, basic_...
#include <utility> // for pair
#include <vector>  // for vector

using namespace std;

namespace RawSpeed {

ThreefrDecoder::ThreefrDecoder(TiffIFD *rootIFD, FileMap* file)  :
    RawDecoder(file), mRootIFD(rootIFD) {
  decoderVersion = 0;
}

ThreefrDecoder::~ThreefrDecoder() { delete mRootIFD; }

RawImage ThreefrDecoder::decodeRawInternal() {
  vector<TiffIFD*> data = mRootIFD->getIFDsWithTag(STRIPOFFSETS);

  if (data.size() < 2)
    ThrowRDE("3FR Decoder: No image data found");

  TiffIFD* raw = data[1];
  uint32 width = raw->getEntry(IMAGEWIDTH)->getInt();
  uint32 height = raw->getEntry(IMAGELENGTH)->getInt();
  uint32 off = raw->getEntry(STRIPOFFSETS)->getInt();

  mRaw->dim = iPoint2D(width, height);
  mRaw->createData();

  HasselbladDecompressor l(*mFile, off, mRaw);
  // We cannot use fully decoding huffman table,
  // because values are packed two pixels at the time.
  l.mFullDecodeHT = false;
  auto pixelOffset = hints.find("pixelBaseOffset");
  if (pixelOffset != hints.end()) {
    stringstream convert((*pixelOffset).second);
    convert >> l.pixelBaseOffset;
  }

  try {
    l.decode(0, 0);
  } catch (IOException &e) {
    mRaw->setError(e.what());
    // Let's ignore it, it may have delivered somewhat useful data.
  }

  return mRaw;
}

void ThreefrDecoder::checkSupportInternal(CameraMetaData *meta) {
  vector<TiffIFD*> data = mRootIFD->getIFDsWithTag(MODEL);
  if (data.empty())
    ThrowRDE("3FR Support check: Model name not found");
  string make = data[0]->getEntry(MAKE)->getString();
  string model = data[0]->getEntry(MODEL)->getString();
  this->checkCameraSupported(meta, make, model, "");
}

void ThreefrDecoder::decodeMetaDataInternal(CameraMetaData *meta) {
  mRaw->cfa.setCFA(iPoint2D(2,2), CFA_RED, CFA_GREEN, CFA_GREEN, CFA_BLUE);
  vector<TiffIFD*> data = mRootIFD->getIFDsWithTag(MODEL);

  if (data.empty())
    ThrowRDE("3FR Decoder: Model name found");
  if (!data[0]->hasEntry(MAKE))
    ThrowRDE("3FR Decoder: Make name not found");

  string make = data[0]->getEntry(MAKE)->getString();
  string model = data[0]->getEntry(MODEL)->getString();
  setMetaData(meta, make, model, "", 0);

  // Fetch the white balance
  if (mRootIFD->hasEntryRecursive(ASSHOTNEUTRAL)) {
    TiffEntry *wb = mRootIFD->getEntryRecursive(ASSHOTNEUTRAL);
    if (wb->count == 3) {
      for (uint32 i=0; i<3; i++)
        mRaw->metadata.wbCoeffs[i] = 1.0f/wb->getFloat(i);
    }
  }
}

} // namespace RawSpeed