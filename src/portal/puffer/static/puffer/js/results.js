'use strict';

const gs_storage_prefix = 'https://storage.googleapis.com/puffer-data-release/';
const gs_console_prefix = 'https://console.cloud.google.com/storage/browser/puffer-data-release/';

// Internal date of datepicker represents the start day of the backup
// (e.g. date = 2020-04-11 UTC => 2020-04-11T11_2020-04-12T11)
function change_date() {
  var first_day = $('#calendar').datepicker('getUTCDate');
  var second_day = new Date(first_day);
  second_day.setDate(second_day.getDate() + 1);

  // format date as e.g. 2020-04-11T11_2020-04-12T11 (UTC)
  var influx_backup_date = first_day.toISOString().substring(0,10) + 'T11_' +
                           second_day.toISOString().substring(0,10) + 'T11';
  $('#selected-date').text(influx_backup_date);

  var permalink = location.protocol + '//'+ location.hostname +
                  (location.port ? ':' + location.port : '') + '/results/' +
                  first_day.toISOString().substring(0,10) + '/';
  $('#permalink').attr('href', permalink);

  var date_dir = gs_storage_prefix + influx_backup_date + '/';

  // CSVs
  const measurements = ['video_sent', 'video_acked', 'client_buffer',
                        'video_size', 'ssim'];
  for (var i = 0; i < measurements.length; i++) {
    var mi = measurements[i];
    var csv_filename = mi + '_' + influx_backup_date + '.csv';
    $('#' + mi).attr('href', date_dir + csv_filename).text(csv_filename);
  }

  // SVGs (display all available -- day and duration should always be available, others may not)
  var alt_text = 'Not available';
  var time_periods = ['day', 'week', 'two_weeks', 'month', 'duration'];
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

  // Model
  var first_day_ISO = first_day.toISOString();
  // Format selected day as YYYYMMDD
  var model_date = first_day_ISO.substring(0,4) + first_day_ISO.substring(5,7) +
                   first_day_ISO.substring(8,10);
  var model_path = 'https://storage.googleapis.com/puffer-models/puffer-ttp/bbr-' +
                   model_date + '-1.tar.gz';
  $('#model').attr('href', model_path).text(model_date + '-1.tar.gz');
}

// main function
$(function() {
  /* Initially display the most recent backup that is ~guaranteed to exist
   * (e.g. if it's currently 08-08 UTC, the 08-07_08-08 backup may not have finished yet,
   * but the 08-06_08-07 backup should have, since it started at 08-07 11AM UTC). */
  var local_date = new Date();
  var utc_date = new Date(Date.UTC(local_date.getUTCFullYear(),
                                   local_date.getUTCMonth(),
                                   local_date.getUTCDate()));
  // works even if date is beginning of month/year
  utc_date.setDate(utc_date.getDate() - 2);

  // initialize bootstrap datepicker
  $('#calendar').datepicker({
    format: 'yyyy-mm-dd',
    startDate: '2019-01-26',
    endDate: utc_date.toISOString().substring(0,10),
    weekStart: 1,
  });

  $('#calendar').on('changeDate', change_date);

  if (input_date == '') {
    // if no date is passed as a URL parameter
    $('#calendar').datepicker('setUTCDate', utc_date);
  } else {
    // display the results on the input date
    var date_in = input_date.split('-');
    var date_in_utc = new Date(Date.UTC(date_in[0], date_in[1] - 1, date_in[2]));
    $('#calendar').datepicker('setUTCDate', date_in_utc);

    // destroy the datepicker (workaround to disable it)
    $('#calendar').datepicker('destroy');
    var results_link = location.protocol + '//'+ location.hostname +
                       (location.port ? ':' + location.port : '') + '/results/';
    $('#permalink').attr('href', results_link);
    $('#permalink').text('select another date');
  }
});
