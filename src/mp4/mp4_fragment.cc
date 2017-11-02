#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

#include "mp4_parser.hh"
#include "mp4_file.hh"
#include "ftyp_box.hh"
#include "sidx_box.hh"
#include "mfhd_box.hh"
#include "tfhd_box.hh"
#include "tfdt_box.hh"

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
  return 0;
}

shared_ptr<FtypBox> create_styp_box()
{
  return make_shared<FtypBox>(
      "styp", // type
      "msdh", // major_brand
      0,      // minor_version
      vector<string>{"msdh", "msix"} // compatible_brands
  );
}

shared_ptr<SidxBox> create_sidx_box(const uint32_t seg_num)
{
  return make_shared<SidxBox>(
      "sidx",          // type
      1,               // version
      0,               // flags
      1,               // reference_id
      30000,           // timescale
      seg_num * 60060, // earlist_presentation_time
      0,               // first_offset
      vector<SidxBox::SidxReference>{ // reference_list
        {false, 0 /* referenced_size */,
         60060 /* subsegment_duration */, true, 4, 0}}
  );
}

shared_ptr<Box> create_moof_box(MP4Parser & mp4_parser, const uint32_t seg_num)
{
  // TODO parse and create trun box
  (void) mp4_parser;

  auto mfhd_box = make_shared<MfhdBox>(
      "mfhd", // type
      0,      // version
      0,      // flags
      seg_num // sequence_number
  );

  auto tfhd_box = make_shared<TfhdBox>(
      "tfhd",  // type
      0,       // version
      0x20008, // flags
      1,       // track_id
      1001     // default_sample_duration
  );

  auto tfdt_box = make_shared<TfdtBox>(
      "tfdt",         // type
      1,              // version
      0,              // flags
      seg_num * 60060 // base_media_decode_time
  );

  auto traf_box = make_shared<Box>("traf");
  traf_box->add_child(move(tfhd_box));
  traf_box->add_child(move(tfdt_box));

  auto moof_box = make_shared<Box>("moof");
  moof_box->add_child(move(mfhd_box));
  moof_box->add_child(move(traf_box));

  return moof_box;
}

void create_media_segment(MP4Parser & mp4_parser, MP4File & output_mp4,
                          const uint32_t seg_num)
{
  auto styp_box = create_styp_box();
  styp_box->write_box(output_mp4);

  auto sidx_box = create_sidx_box(seg_num);
  sidx_box->write_box(output_mp4);

  auto moof_box = create_moof_box(mp4_parser, seg_num);
  moof_box->write_box(output_mp4);

  auto mdat_box = mp4_parser.find_first_box_of("mdat");
  mdat_box->write_box(output_mp4);
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
