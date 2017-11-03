#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "strict_conversions.hh"
#include "mp4_parser.hh"
#include "mp4_file.hh"
#include "ftyp_box.hh"
#include "sidx_box.hh"
#include "mfhd_box.hh"
#include "tfhd_box.hh"
#include "tfdt_box.hh"
#include "stsz_box.hh"
#include "ctts_box.hh"
#include "trun_box.hh"

using namespace std;
using namespace MP4;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <input.mp4> <output.m4s>\n\n"
  "<input.mp4>     input MP4 file to fragment\n"
  "<output.m4s>    output M4S file"
  << endl;
}

uint32_t get_segment_number(const string & mp4_filename)
{
  // TODO: get segment number from filename
  (void) mp4_filename;
  return 1;
}

void create_styp_box(MP4File & output_mp4)
{
  auto styp_box = make_shared<FtypBox>(
      "styp", // type
      "msdh", // major_brand
      0,      // minor_version
      vector<string>{"msdh", "msix"} // compatible_brands
  );

  styp_box->write_box(output_mp4);
}

unsigned int create_sidx_box(MP4File & output_mp4, const uint32_t seg_num)
{
  auto sidx_box = make_shared<SidxBox>(
      "sidx",          // type
      1,               // version
      0,               // flags
      1,               // reference_id
      30000,           // timescale
      seg_num * 60060, // earlist_presentation_time
      0,               // first_offset
      vector<SidxBox::SidxReference>{ // reference_list
        {false, 0 /* referenced_size, will be filled in later */,
         60060 /* subsegment_duration */, true, 4, 0}}
  );

  sidx_box->write_box(output_mp4);

  return sidx_box->reference_list_pos();
}

vector<TrunBox::Sample> create_samples(MP4Parser & mp4_parser)
{
  vector<TrunBox::Sample> samples;

  auto stsz_box = static_pointer_cast<StszBox>(
                      mp4_parser.find_first_box_of("stsz"));
  auto ctts_box = static_pointer_cast<CttsBox>(
                      mp4_parser.find_first_box_of("ctts"));

  const vector<uint32_t> & size_entries = stsz_box->entries();
  const vector<int64_t> & offset_entries = ctts_box->entries();

  if (size_entries.size() != offset_entries.size()) {
    throw runtime_error(
        "stsz and ctts boxes should have the same number of entries");
  }

  for (unsigned int i = 0; i < size_entries.size(); ++i) {
    samples.emplace_back(TrunBox::Sample{
        1001,             // sample_duration
        size_entries[i],  // sample_size
        0,                // sample_flags (not present)
        offset_entries[i] // sample_composition_time_offset
    });
  }

  return samples;
}

void create_moof_box(MP4Parser & mp4_parser,
                     MP4File & output_mp4,
                     const uint32_t seg_num)
{
  auto mfhd_box = make_shared<MfhdBox>(
      "mfhd", // type
      0,      // version
      0,      // flags
      seg_num // sequence_number
  );

  auto tfhd_box = make_shared<TfhdBox>(
      "tfhd",   // type
      0,        // version
      0x20028,  // flags
      1,        // track_id
      1001,     // default_sample_duration
      0x1010000 // default_sample_flags
  );

  auto tfdt_box = make_shared<TfdtBox>(
      "tfdt",         // type
      1,              // version
      0,              // flags
      seg_num * 60060 // base_media_decode_time
  );

  vector<TrunBox::Sample> samples = create_samples(mp4_parser);

  auto trun_box = make_shared<TrunBox>(
      "trun",        // type
      0,             // version
      0xa05,         // flags
      move(samples), // samples
      0,             // data_offset, will be filled in once moof is created
      0x2000000      // first_sample_flags
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

void create_mdat_box(MP4Parser & mp4_parser, MP4File & output_mp4)
{
  auto mdat_box = mp4_parser.find_first_box_of("mdat");
  mdat_box->write_box(output_mp4);
}

void create_media_segment(MP4Parser & mp4_parser, MP4File & output_mp4,
                          const uint32_t seg_num)
{
  create_styp_box(output_mp4);

  uint64_t sidx_offset = output_mp4.curr_offset();
  unsigned int sidx_reference_list_pos = create_sidx_box(output_mp4, seg_num);

  uint64_t moof_offset = output_mp4.curr_offset();
  create_moof_box(mp4_parser, output_mp4, seg_num);
  create_mdat_box(mp4_parser, output_mp4);

  /* fill in 'referenced_size' in sidx box */
  uint32_t referenced_size = narrow_cast<uint32_t>(
      output_mp4.curr_offset() - moof_offset);
  /* set referenced_size's most significant bit to 0 (reference_type) */
  output_mp4.write_uint32_at(referenced_size & 0x7FFFFFFF,
                             sidx_offset + sidx_reference_list_pos);
}

void fragment(MP4Parser & mp4_parser, MP4File & output_mp4,
              const uint32_t seg_num)
{
  create_media_segment(mp4_parser, output_mp4, seg_num);
}

int main(int argc, char * argv[])
{
  if (argc < 1) {
    abort();
  }

  if (argc != 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  string input_mp4 = argv[1];
  uint32_t seg_num = get_segment_number(input_mp4);

  MP4Parser mp4_parser(input_mp4);
  mp4_parser.parse();

  MP4File output_mp4(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);

  fragment(mp4_parser, output_mp4, seg_num);

  return EXIT_SUCCESS;
}
