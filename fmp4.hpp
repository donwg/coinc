#pragma once

#include "mp4box.hpp"

namespace libfmp4 {

static u32 _version_flag(u32 v, u32 f) {
  v = (v << 24) | f;
  return v;
}
namespace box {
#pragma pack(1)
struct mvex {
  u32 size;
  u32 type;
  struct {
    u32 size;
    u32 type;
    u32 verflags; //
    u32 track_id;
    u32 default_sample_descriptionIndex;
    u32 default_sample_duration;
    u32 default_sample_size;
    u32 default_sample_flags;
  } trex;
  void *Marshal();
};

inline void *mvex::Marshal() {
  size = libmp4::Htobe32_Sizeof(mvex);
  type = libmp4::Le32Type("mvex");
  trex.size = libmp4::Htobe32_Sizeof(trex);
  trex.type = libmp4::Le32Type("trex");
  trex.verflags = libmp4::Htobe32(_version_flag(0, 0));
  trex.default_sample_descriptionIndex = libmp4::Htobe32(1);
  trex.track_id = libmp4::Htobe32(1);
  return this;
}

struct moof {
  u32 size;
  u32 type;
  struct {
    u32 size;
    u32 type;
    u32 verflags;
    u32 sequenceNumber;
  } mfhd;
  struct {
    u32 size;
    u32 type;
    struct {
      u32 size;
      u32 type;
      u32 verflags;
      u32 trackId;
    } tfhd;
    struct {
      u32 size;
      u32 type;
      u32 verflags; // ver=1 64位decode_time, 默认0
      u32 decode_time;
    } tfdt;
    struct {
      u32 size;
      u32 type;
      u32 verflags;
      u32 sample_count;
      u32 data_offset; // 默认0
      struct {
        u32 duration;
        u32 size;
        u32 flags;
        u32 time_offset;
      } sample;
    } trun;
  } traf;
  libmp4::box::mdat mdatv;
  void Marshal(u32 trackId);
};

inline void moof::Marshal(u32 trackId) {
  size = libmp4::Htobe32(sizeof(moof) - sizeof(libmp4::box::mdat));
  type = libmp4::Le32Type("moof");

  mfhd.size = libmp4::Htobe32_Sizeof(mfhd);
  mfhd.type = libmp4::Le32Type("mfhd");
  mfhd.verflags = libmp4::Htobe32(_version_flag(0, 0));
  // mfhd.sequenceNumber = libmp4::Htobe32(seq);

  traf.size = libmp4::Htobe32_Sizeof(traf);
  traf.type = libmp4::Le32Type("traf");

  traf.tfhd.size = libmp4::Htobe32_Sizeof(traf.tfhd);
  traf.tfhd.type = libmp4::Le32Type("tfhd");
  traf.tfhd.verflags = libmp4::Htobe32(_version_flag(0, 0x020000));
  traf.tfhd.trackId = libmp4::Htobe32(trackId);

  traf.tfdt.size = libmp4::Htobe32_Sizeof(traf.tfdt);
  traf.tfdt.type = libmp4::Le32Type("tfdt");
  traf.tfdt.verflags = libmp4::Htobe32(_version_flag(0, 0));
  // traf.tfdt.decode_time = libmp4::Htobe32(traf.tfdt.decode_time);

  traf.trun.size = libmp4::Htobe32_Sizeof(traf.trun);
  traf.trun.type = libmp4::Le32Type("trun");
  traf.trun.verflags = libmp4::Htobe32(_version_flag(1, 0xf01));
  traf.trun.data_offset = libmp4::Htobe32_Sizeof(moof); // moof+mdat_header

  traf.trun.sample_count = libmp4::Htobe32(1);
  traf.trun.sample.time_offset = 0;

  mdatv.type = libmp4::Le32Type("mdat");
}
} // namespace box
#pragma pack()
// fmp4
// ftyp+moov+(moof+mdat,moof+mdat,....)
class Writer {
private:
  FILE *file_;
  int64_t firts_;
  int64_t lsts_;
  uint32_t seq_;
  box::moof moofv_;

public:
  Writer() : file_(nullptr), firts_(0), lsts_(0), seq_(0) {}
  Writer(const char *filename) : Writer() { this->Open(filename); }
  ~Writer() { this->Close(); }
  bool Open(const char *filename) {
    file_ = fopen(filename, "wb+");
    return file_ != NULL;
  }
  void Close();
  int WriteVideo(int64_t ts, bool iskey, char *data, size_t len);

private:
  size_t WriteBoxFtypMoov(nalu::Vector &nalus) {
    libmp4::box::ftyp ftyp = {0};
    libmp4::box::moov moov = {0};
    box::mvex mvex = {0};
    libmp4::Trak trakv;
    ftyp.compat3 = trakv.MakeVideo(nalus);
    moofv_.Marshal(1);
    int vlen = trakv.Marshal();
    fwrite(ftyp.Marshal(), sizeof(ftyp), 1, file_);
    fwrite(moov.Marshal(vlen + sizeof(box::mvex)), sizeof(moov), 1, file_);
    fwrite(mvex.Marshal(), sizeof(box::mvex), 1, file_);
    return fwrite(trakv.Value(), vlen, 1, file_);
  }
};

inline void Writer::Close() {
  if (file_) {
    // 更新时长
    u32 durOffset = sizeof(libmp4::box::ftyp) + 32;
    u32 dur = libmp4::Htobe32(lsts_ - firts_);
    fseek(file_, durOffset, SEEK_SET);
    fwrite(&dur, sizeof(u32), 1, file_);
    fclose(file_);
    file_ = NULL;
  }
}

inline int Writer::WriteVideo(int64_t ts, bool iskey, char *data, size_t len) {
  if (file_ == NULL) {
    return -1;
  }
  nalu::Vector nalus;
  char *ptr = nalu::Split(data, len, nalus);
  if (iskey && firts_ == 0) {
    this->WriteBoxFtypMoov(nalus);
    firts_ = lsts_ = ts;
  }
  if (firts_ == 0) {
    return 0;
  }
  // 写入moof
  moofv_.mfhd.sequenceNumber = libmp4::Htobe32(seq_++);
  moofv_.mdatv.size = libmp4::Htobe32(len + 4 + sizeof(libmp4::box::mdat));
  moofv_.traf.tfdt.decode_time = libmp4::Htobe32(ts - firts_);
  moofv_.traf.trun.sample.duration = libmp4::Htobe32(ts - lsts_);
  moofv_.traf.trun.sample.size = libmp4::Htobe32(len + 4);
  moofv_.traf.trun.sample.flags =
      libmp4::Htobe32(iskey ? 0x02000000 : 0x00010000);
  lsts_ = ts;
  fwrite(&moofv_, sizeof(box::moof), 1, file_);
  // 写入mdat
  u32 slen = libmp4::Htobe32(len);
  fwrite(&slen, sizeof(u32), 1, file_);
  return int(fwrite(ptr, len, 1, file_));
}
} // namespace libfmp4
