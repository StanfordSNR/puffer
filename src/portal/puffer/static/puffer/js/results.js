'use strict';

const gs_prefix = 'https://storage.googleapis.com/puffer-stanford-public/data-release-test/';

function change_date() {
  // selected date as string, formatted as 'yyyy-mm-dd'
  var date = $('#calendar').datepicker('getFormattedDate');

  // TODO: use jQuery to change text as necessary
  $('.selected-date').text(date);

  // TODO: construct the URLs of data files
  var video_sent_csv = 'video_sent_XXX' + date + 'XXX.csv';
  $('#video_sent_csv').attr('href', gs_prefix + video_sent_csv).text(video_sent_csv);

  // TODO: display SVG figures
  var perf_today_svg = 'XXX' + date + 'XXX.svg';
  $('#perf-today').attr('src', gs_prefix + perf_today_svg).attr('alt', perf_today_svg);
}

$(function() {
  // initialize bootstrap datepicker
  $('#calendar').datepicker({
    'format': 'yyyy-mm-dd',
  });

  $('#calendar').on('changeDate', change_date);

  // set the first selected date to today in UTC
  var local = new Date();
  var utc_date = new Date(Date.UTC(local.getUTCFullYear(),
                                   local.getUTCMonth(),
                                   local.getUTCDate()));
  $('#calendar').datepicker('setUTCDate', utc_date);
});
