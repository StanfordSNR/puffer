#include <getopt.h>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

#include "filesystem.hh"
#include "strict_conversions.hh"
#include "tokenize.hh"
#include "mp4_parser.hh"
#include "mp4_file.hh"
#include "ftyp_box.hh"
#include "mvhd_box.hh"
#include "tkhd_box.hh"
#include "elst_box.hh"
#include "mdhd_box.hh"
#include "trex_box.hh"
#include "sidx_box.hh"
#include "mfhd_box.hh"
#include "tfhd_box.hh"
#include "tfdt_box.hh"
#include "stsz_box.hh"
#include "ctts_box.hh"
#include "stts_box.hh"
#include "stsc_box.hh"
#include "stco_box.hh"
#include "trun_box.hh"

using namespace std;
using namespace MP4;

const uint32_t global_timescale = 90000;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " [options] <input_segment>\n\n"
  "<input_segment>    input MP4 segment to fragment\n\n"
  "Options:\n"
  "--init-segment, -i     output initial segment\n"
  "--media-segment, -m    output media segment in the format of <num>.m4s,\n"
  "                       where <num> denotes the segment number"
  << endl;
}

uint64_t scale_global_timestamp(const uint64_t global_timestamp,
                                const uint32_t new_timescale)
{
  /* scale the timestamp in global timescale to the new_timescale */
  double sec = static_cast<double>(global_timestamp) / global_timescale;
  return narrow_round<uint64_t>(sec * new_timescale);
}

void create_ftyp_box(MP4Parser & mp4_parser, MP4File & output_mp4)
{
  /* Create ftyp box and add compatible brand */
  auto ftyp_box = static_pointer_cast<FtypBox>(
      mp4_parser.find_first_box_of("ftyp"));
  ftyp_box->add_compatible_brand("iso5");
  ftyp_box->write_box(output_mp4);
}

void create_moov_box(MP4Parser & mp4_parser, MP4File & output_mp4)
{
  /* create mvhd box and set duration to 0 */
  auto mvhd_box = static_pointer_cast<MvhdBox>(
      mp4_parser.find_first_box_of("mvhd"));
  mvhd_box->set_duration(0);

  /* set duration to 0 in tkhd box */
  auto tkhd_box = static_pointer_cast<TkhdBox>(
      mp4_parser.find_first_box_of("tkhd"));
  tkhd_box->set_duration(0);

  /* set segment duration to 0 in elst box */
  auto elst_box = static_pointer_cast<ElstBox>(
      mp4_parser.find_first_box_of("elst"));
  elst_box->set_segment_duration(0);

  /* set duration to 0 in mdhd box */
  auto mdhd_box = static_pointer_cast<MdhdBox>(
      mp4_parser.find_first_box_of("mdhd"));
  mdhd_box->set_duration(0);

  /* remove stss and ctts boxes from stbl box */
  auto stbl_box = mp4_parser.find_first_box_of("stbl");
  stbl_box->remove_child("stss");
  stbl_box->remove_child("ctts");

  /* clear the entries in stts, stsc, stsz, stco boxes */
  auto stts_box = static_pointer_cast<SttsBox>(stbl_box->find_child("stts"));
  stts_box->set_entries({});

  auto stsc_box = static_pointer_cast<StscBox>(stbl_box->find_child("stsc"));
  stsc_box->set_entries({});

  auto stsz_box = static_pointer_cast<StszBox>(stbl_box->find_child("stsz"));
  stsz_box->set_sample_size(0);
  stsz_box->set_entries({});

  auto stco_box = static_pointer_cast<StcoBox>(stbl_box->find_child("stco"));
  stco_box->set_entries({});

  /* create mvex box */
  auto trex_box = make_shared<TrexBox>(
      "trex",  // type
      0,       // version
      0,       // flags,
      1,       // track_id
      1,       // default_sample_description_index
      0,       // default_sample_duration
      0,       // default_sample_size
      0        // default_sample_flags
  );
  auto mvex_box = make_shared<Box>("mvex");
  mvex_box->add_child(move(trex_box));

  /* create moov box; insert mvex box after trak box */
  auto moov_box = mp4_parser.find_first_box_of("moov");
  moov_box->insert_child(move(mvex_box), "trak");
  moov_box->write_box(output_mp4);
}

void create_init_segment(MP4Parser & mp4_parser, MP4File & output_mp4)
{
  create_ftyp_box(mp4_parser, output_mp4);
  create_moov_box(mp4_parser, output_mp4);
}

void create_styp_box(MP4File & output_mp4)
{
  auto styp_box = make_shared<FtypBox>(
      "styp",  // type
      "msdh",  // major_brand
      0,       // minor_version
      vector<string>{"msdh", "msix"}  // compatible_brands
  );

  styp_box->write_box(output_mp4);
}

unsigned int create_sidx_box(MP4Parser & mp4_parser, MP4File & output_mp4,
                             const uint64_t global_timestamp)
{
  auto mdhd_box = static_pointer_cast<MdhdBox>(
      mp4_parser.find_first_box_of("mdhd"));
  uint32_t timescale = mdhd_box->timescale();
  uint32_t duration = narrow_cast<uint32_t>(mdhd_box->duration());

  uint64_t mp4_ts = scale_global_timestamp(global_timestamp, timescale);

  auto sidx_box = make_shared<SidxBox>(
      "sidx",     // type
      1,          // version
      0,          // flags
      1,          // reference_id
      timescale,  // timescale
      mp4_ts,     // earlist_presentation_time
      0,          // first_offset
      vector<SidxBox::SidxReference>{  // reference_list
        {false, 0 /* referenced_size, will be filled in later */,
         duration /* subsegment_duration */, true, 4, 0}
      }
  );

  sidx_box->write_box(output_mp4);

  return sidx_box->reference_list_pos();
}

uint32_t check_sample_count(const uint32_t size_cnt,
                            const uint32_t duration_cnt,
                            const uint32_t offset_cnt)
{
  vector<uint32_t> non_zero_cnt;
  if (size_cnt > 0) {
    non_zero_cnt.emplace_back(size_cnt);
  }
  if (duration_cnt > 0) {
    non_zero_cnt.emplace_back(duration_cnt);
  }
  if (offset_cnt > 0) {
    non_zero_cnt.emplace_back(offset_cnt);
  }

  uint32_t same_cnt = 0;
  for (const auto & cnt : non_zero_cnt) {
    if (same_cnt == 0) {
      same_cnt = cnt;
    } else if (same_cnt != cnt) {
      throw runtime_error("inconsistent sample count");
    }
  }

  return same_cnt;
}

vector<TrunBox::Sample> create_samples(MP4Parser & mp4_parser,
                                       const uint32_t trun_flags)
{
  vector<TrunBox::Sample> samples;

  vector<uint32_t> size_entries;
  if (trun_flags & TrunBox::sample_size_present) {
    auto stsz_box = static_pointer_cast<StszBox>(
                        mp4_parser.find_first_box_of("stsz"));

    size_entries = stsz_box->entries();
  }

  vector<uint32_t> duration_entries;
  if (trun_flags & TrunBox::sample_duration_present) {
    auto stts_box = static_pointer_cast<SttsBox>(
                        mp4_parser.find_first_box_of("stts"));

    for (const auto & stts_entry : stts_box->entries()) {
      for (uint32_t i = 0; i < stts_entry.sample_count; ++i) {
        duration_entries.emplace_back(stts_entry.sample_delta);
      }
    }
  }

  vector<uint32_t> offset_entries;
  if (trun_flags & TrunBox::sample_composition_time_offsets_present) {
    auto ctts_box = static_pointer_cast<CttsBox>(
                        mp4_parser.find_first_box_of("ctts"));

    for (const auto & ctts_entry : ctts_box->entries()) {
      for (uint32_t i = 0; i < ctts_entry.sample_count; ++i) {
        offset_entries.emplace_back(ctts_entry.sample_offset);
      }
    }
  }

  /* sanity check for consistent sample count */
  uint32_t size_cnt = size_entries.size();
  uint32_t duration_cnt = duration_entries.size();
  uint32_t offset_cnt = offset_entries.size();
  uint32_t sample_cnt = check_sample_count(size_cnt, duration_cnt, offset_cnt);

  for (unsigned int i = 0; i < sample_cnt; ++i) {
    samples.push_back({
      duration_cnt ? duration_entries[i] : 0,  // sample_duration
      size_cnt ? size_entries[i] : 0,      // sample_size
      0,                                   // sample_flags (not present)
      offset_cnt ? offset_entries[i] : 0,  // sample_composition_time_offset
    });
  }

  return samples;
}

uint32_t get_default_sample_duration(MP4Parser & mp4_parser)
{
  auto stts_box = static_pointer_cast<SttsBox>(
      mp4_parser.find_first_box_of("stts"));

  auto stts_entries = stts_box->entries();
  if (stts_entries.size() == 1) {
    return stts_entries[0].sample_delta;
  }

  return 0;
}

uint32_t get_default_sample_size(MP4Parser & mp4_parser)
{
  auto stsz_box = static_pointer_cast<StszBox>(
      mp4_parser.find_first_box_of("stsz"));

  return stsz_box->sample_size();
}

void create_moof_box(MP4Parser & mp4_parser, MP4File & output_mp4,
                     const uint64_t global_timestamp)
{
  auto mdhd_box = static_pointer_cast<MdhdBox>(
      mp4_parser.find_first_box_of("mdhd"));
  uint32_t timescale = mdhd_box->timescale();
  uint32_t duration = narrow_cast<uint32_t>(mdhd_box->duration());

  uint64_t mp4_ts = scale_global_timestamp(global_timestamp, timescale);
  uint32_t sequence_number = narrow_round<uint32_t>(
                               static_cast<double>(mp4_ts) / duration);

  auto mfhd_box = make_shared<MfhdBox>(
      "mfhd",         // type
      0,              // version
      0,              // flags
      sequence_number
  );

  /* create flags for tfhd and trun boxes */
  uint32_t tfhd_flags = TfhdBox::default_base_is_moof |
                        TfhdBox::default_sample_flags_present;
  uint32_t trun_flags = TrunBox::data_offset_present;

  uint32_t default_sample_duration = get_default_sample_duration(mp4_parser);
  if (default_sample_duration) {
    tfhd_flags |= TfhdBox::default_sample_duration_present;
  } else {
    trun_flags |= TrunBox::sample_duration_present;
  }

  uint32_t default_sample_size = get_default_sample_size(mp4_parser);
  if (default_sample_size) {
    tfhd_flags |= TfhdBox::default_sample_size_present;
  } else {
    trun_flags |= TrunBox::sample_size_present;
  }

  uint32_t default_sample_flags, first_sample_flags;
  if (mp4_parser.is_video()) {
    default_sample_flags = 0x1010000;
    first_sample_flags = 0x2000000;
    trun_flags |= TrunBox::first_sample_flags_present;

    if (mp4_parser.find_first_box_of("ctts") != nullptr) {
      trun_flags |= TrunBox::sample_composition_time_offsets_present;
    }
  } else if (mp4_parser.is_audio()) {
    default_sample_flags = 0x2000000;
    first_sample_flags = 0;
  }

  auto tfhd_box = make_shared<TfhdBox>(
      "tfhd",      // type
      0,           // version
      tfhd_flags,  // flags
      1,           // track_id
      default_sample_duration,
      default_sample_size,
      default_sample_flags
  );

  auto tfdt_box = make_shared<TfdtBox>(
      "tfdt",  // type
      1,       // version
      0,       // flags
      mp4_ts   // base_media_decode_time
  );

  vector<TrunBox::Sample> samples = create_samples(mp4_parser, trun_flags);

  auto trun_box = make_shared<TrunBox>(
      "trun",         // type
      0,              // version
      trun_flags,     // flags
      move(samples),  // samples
      0,              // data_offset, will be filled in once moof is created
      first_sample_flags
  );

  /* write boxes one by one to get the position of 'data_offset' */
  uint64_t moof_offset = output_mp4.curr_offset();
  auto moof_box = make_shared<Box>("moof");
  moof_box->write_size_type(output_mp4);
  mfhd_box->write_box(output_mp4);

  uint64_t traf_offset = output_mp4.curr_offset();
  auto traf_box = make_shared<Box>("traf");
  traf_box->write_size_type(output_mp4);
  tfhd_box->write_box(output_mp4);
  tfdt_box->write_box(output_mp4);

  uint64_t trun_offset = output_mp4.curr_offset();
  trun_box->write_box(output_mp4);

  traf_box->fix_size_at(output_mp4, traf_offset);
  moof_box->fix_size_at(output_mp4, moof_offset);

  /* fill in 'data_offset' in trun box at 'trun_offset + 16'
   * data_offset = size of moof + header size of mdat (8) */
  uint64_t moof_size = output_mp4.curr_offset() - moof_offset;
  int32_t data_offset_value = narrow_cast<int32_t>(moof_size + 8);
  output_mp4.write_int32_at(data_offset_value,
                            trun_offset + trun_box->data_offset_pos());
}

void create_media_segment(MP4Parser & mp4_parser, MP4File & output_mp4,
                          const uint64_t global_timestamp)
{
  create_styp_box(output_mp4);

  /* create sidx box and save the position of referenced_size */
  uint64_t sidx_offset = output_mp4.curr_offset();
  unsigned int sidx_ref_list_pos = create_sidx_box(mp4_parser, output_mp4,
                                                   global_timestamp);

  uint64_t moof_offset = output_mp4.curr_offset();
  create_moof_box(mp4_parser, output_mp4, global_timestamp);

  auto mdat_box = mp4_parser.find_first_box_of("mdat");
  mdat_box->write_box(output_mp4);

  /* fill in 'referenced_size' = size of moof + size of mdat in sidx box */
  uint32_t referenced_size = narrow_cast<uint32_t>(
      output_mp4.curr_offset() - moof_offset);
  /* set referenced_size's most significant bit to 0 (reference_type) */
  output_mp4.write_uint32_at(referenced_size & 0x7FFFFFFF,
                             sidx_offset + sidx_ref_list_pos);
}

uint64_t get_timestamp(const string & filepath)
{
  return narrow_cast<uint64_t>(stoll(fs::path(filepath).stem()));
}

void fragment(const string & input_mp4,
              const string & init_segment,
              const string & media_segment)
{
  MP4Parser mp4_parser(input_mp4);
  /* skip parsing avc1 and mp4a boxes (if exist) but save them as raw data */
  mp4_parser.ignore_box("avc1");
  mp4_parser.ignore_box("mp4a");
  mp4_parser.parse();

  if (not mp4_parser.is_video() and not mp4_parser.is_audio()) {
    throw runtime_error("input MP4 is not a supported video or audio");
  }

  /* run create_init_segment last so it can safely make changes to parser */
  if (media_segment.size()) {
    /* get timestamp in global timescale from input MP4's filename */
    const uint64_t global_timestamp = get_timestamp(input_mp4);

    MP4File output_mp4(media_segment, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    create_media_segment(mp4_parser, output_mp4, global_timestamp);
  }

  if (init_segment.size()) {
    MP4File output_mp4(init_segment, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    create_init_segment(mp4_parser, output_mp4);
  }
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  string init_segment, media_segment;

  const option cmd_line_opts[] = {
    {"init-segment",  required_argument, nullptr, 'i'},
    {"media-segment", required_argument, nullptr, 'm'},
    { nullptr,        0,                 nullptr,  0 }
  };

  while (true) {
    const int opt = getopt_long(argc, argv, "i:m:", cmd_line_opts, nullptr);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'i':
      init_segment = optarg;
      break;
    case 'm':
      media_segment = optarg;
      break;
    default:
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (optind != argc - 1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string input_segment = argv[optind];

  if (init_segment.empty() and media_segment.empty()) {
    cerr << "Error: at least one of -i and -m is required" << endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  fragment(input_segment, init_segment, media_segment);

  return EXIT_SUCCESS;
}
