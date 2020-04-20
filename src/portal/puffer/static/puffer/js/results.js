'use strict';

const gs_storage_prefix = 'https://storage.googleapis.com/puffer-stanford-public/data-release/';
const gs_console_prefix = 'https://console.cloud.google.com/storage/browser/puffer-stanford-public/data-release/';

// Internal date of datepicker represents the start day of the backup
// (e.g. date = 2020-04-11 UTC => 2020-04-11T11_2020-04-12T11)
function change_date() {
  var first_day = $('#calendar').datepicker('getUTCDate');
  var second_day = new Date(first_day);
  second_day.setDate(first_day.getDate() + 1);
  // format date as e.g. 2020-04-11T11_2020-04-12T11
  var influx_backup_date = first_day.toISOString().substring(0,10) + 'T11_' +
                           second_day.toISOString().substring(0,10) + 'T11';
  console.log(influx_backup_date);

  $('.selected-date').text(influx_backup_date);

  var date_dir = gs_storage_prefix + influx_backup_date + '/';

  // CSVs
  var csv_filename = 'video_sent_' + influx_backup_date + '.csv';
  $('#video_sent_csv').attr('href', date_dir + csv_filename).text(csv_filename);

  csv_filename = 'client_buffer_' + influx_backup_date + '.csv';
  $('#client_buffer_csv').attr('href', date_dir + csv_filename).text(csv_filename);

  csv_filename = 'video_acked_' + influx_backup_date + '.csv';
  $('#video_acked_csv').attr('href', date_dir + csv_filename).text(csv_filename);

  // SVGs (display all available -- day should always be available, others may not)
  var alt_text = 'Not available';
  var time_periods = ['day', 'week', 'two_weeks', 'month'];
  var speeds = ['all', 'slow'];
  for (var period = 0; period < time_periods.length; period++) {
    for (var speed = 0; speed < speeds.length; speed++) {
      var plot_prefix = time_periods[period] + '_' + speeds[speed] + '_plot';
      var svg_filename = plot_prefix + '_' + influx_backup_date + '.svg';
      $('#' + plot_prefix).attr('src', date_dir + svg_filename).attr('alt', alt_text);
    }
  }

  // Bucket
  // Can't link to a directory via storage prefix
  date_dir = gs_console_prefix + influx_backup_date + '/';
  $('#bucket').attr('href', date_dir).text('Storage bucket');
}

$(function() {
  // initialize bootstrap datepicker
  $('#calendar').datepicker({
    format: 'yyyy-mm-dd',
    startDate: '2019-01-01',
    endDate: '-2d',
  });

  $('#calendar').on('changeDate', change_date);

  /* Initially display the most recent backup that is ~guaranteed to exist
   * (e.g. if it's currently 08-08 UTC, the 08-07_08-08 backup may not have finished yet,
   * but the 08-06_08-07 backup should have, since it started at 08-07 11AM UTC). */
  var utc_date = new Date(Date.now());    // now() returns ms UTC
  // works even if date is beginning of month/year
  utc_date.setDate(utc_date.getDate() - 2);
  $('#calendar').datepicker('setUTCDate', utc_date);
});
