<?php

/**
 * @file
 * Provides an interface for managing customer's GoCardless mandates.
 */

/**
 * Displays the GoCardless Payments edit screen .
 */
function uc_gc_client_payments_form($form, &$form_state, $order) {

  $gcs = db_select('uc_gc_client', 'g')
    ->fields('g')
    ->condition('ucid', $order->order_id)
    ->execute()->fetchAllAssoc('ucpid');

  $count = count($gcs);

  if ($count == 0) {
    $no_gcs = 'You have no GoCardless payments associated with this order.';
    $form['no_orders'] = array(
      '#type' => 'item',
      '#markup' => $no_gcs,
    );
    return $form;
  }

  // Create an array of unique product names.
  $rows = array();
  $product_select = array();
  $title_count = array();
  foreach ($order->products as $ucpid => $product) {
    !isset($title_count[$product->title]) ? $title_count[$product->title] = 1 : $title_count[$product->title]++;
    $title_count[$product->title] == 1 ? $title[$ucpid] = $product->title : $title[$ucpid] = $product->title . ' (' . $title_count[$product->title] . ')';
    $product_select[$ucpid] = $title[$ucpid];
  }
  // dpm($product_select);
  if ($count == 1) {

    $ucpid = reset($gcs)->ucpid;
    $text_top = t('<h3><b>Administrate <a href = "@url">@title</a></b></h3>', array('@title' => $product_select[$ucpid], '@url' => url('admin/store/orders/' . $order->order_id)));
    $form['text_top'] = array(
      '#type' => 'item',
      '#markup' => $text_top,
    );
    $form['ucpid'] = array(
      '#type' => 'value',
      '#value' => $ucpid,
    );
  }
  else {
    if (!empty($form_state['input']['ucpid'])) {
      $ucpid = $form_state['input']['ucpid'];
    }
    elseif (isset($_SESSION['change_ucpid'])) {
      $ucpid = $_SESSION['change_ucpid']['ucpid'];
    }
    elseif (!isset($ucpid)) {
      $ucpid = reset($gcs)->ucpid;
    }

    $form = array(
      '#prefix' => '<div id="change-product">',
      '#suffix' => '</div>',
    );

    $text_top = t('<h3><b>Administrate <a href = "@url">@title</a></b></h3>', array('@title' => $product_select[$ucpid], '@url' => url('admin/store/orders/' . $order->order_id)));

    $form['text_top'] = array(
      '#type' => 'item',
      '#markup' => $text_top,
    );

    $form['ucpid'] = array(
      '#title' => t('Switch product'),
      '#type' => 'select',
      '#options' => $product_select,
      '#default_value' => $ucpid,
      '#ajax' => array(
        'callback' => 'uc_gc_client_payments_form_callback',
        'wrapper' => 'change-product',
        'method' => 'replace',
      ),
    );
  }

  $gc = $gcs[$ucpid];
  $params = array(
    'endpoint' => 'mandates',
    'action' => 'get',
    'mandate' => $gc->gcid,
  );
  $result = uc_gc_client_api($params);
  $mandate = $result->response->body->mandates;
  if($mandate->scheme == 'bacs') $sign = '£';
  elseif($mandate->scheme == 'sepa_core') $sign = '€';
  else $sign = 'kr';

  $product = $order->products[$ucpid];

  $interval = $product->data['interval_params']['length'] . ' ' . $product->data['interval_params']['unit'];

  if ($gc->type == 'P') {

    // Payments.
    $type = 'One-off payments';
    $header = array('product', 'payment type', 'cost', 'price', 'payment interval', 'next scheduled payment creation', 'next possible charge date', 'gocardless id', 'created', 'status');

    $payment_header = array('Payment name', 'Payment ID', 'Amount', 'Created at', 'Status', 'Charge customer at');

    $rows[$product->order_product_id] = array(
      $product_select[$ucpid],
      $type,
      uc_currency_format($product->cost),
      uc_currency_format($product->price),
      $interval == ' ' ? 'Not set' : $interval,
      !is_null($gc->next_payment) ? format_date($gc->next_payment, 'medium') : 'None',
      format_date(strtotime($mandate->next_possible_charge_date), 'uc_store'),
      $mandate->id,
      format_date(strtotime($mandate->created_at), 'uc_store'),
      $mandate->status,
    );
    $params = array(
      'endpoint' => 'payments',
      'action' => 'list',
      'mandate' => $gc->gcid,
    );
    $result = uc_gc_client_api($params);
    $payments = $result->response->body->payments;
  }
  else {

    // Subscriptions.
    $type = 'Subscription';
    $header = array('product', 'payment type', 'cost', 'price', 'payment interval', 'next possible charge date', 'gocardless id', 'created', 'status');

    $payment_header = array('Payment name', 'Payment ID', 'Amount', 'Created at', 'Status', 'Charge customer at');

    $rows[$product->order_product_id] = array(
      $product_select[$ucpid],
      $type,
      uc_currency_format($product->cost),
      uc_currency_format($product->price),
      $interval,
      format_date(strtotime($mandate->next_possible_charge_date), 'uc_store'),
      $mandate->id,
      format_date(strtotime($mandate->created_at), 'uc_store'),
      $mandate->status,
    );

    $params = array(
      'endpoint' => 'subscriptions',
      // 'endpoint' => 'payments',.
      'action' => 'list',
      'mandate' => $gc->gcid,
    );
    $result = uc_gc_client_api($params);
    $subscriptions = $result->response->body->subscriptions;

    foreach ($subscriptions as $subscription) {
      if (isset($subscription->metadata->ucpid)) {
        if ($subscription->metadata->ucpid == $ucpid) {
          $subscription_id = $subscription->id;
          break;
        }
      }
    }

    $params = array(
      'endpoint' => 'payments',
      'action' => 'list',
      'subscription' => $subscription_id,
    );
    $result = uc_gc_client_api($params);
    $payments = $result->response->body->payments;
  }

  $payment_rows = array();
  foreach ($payments as &$payment) {
    $payment_rows[] = array(
      $payment->description,
      $payment->id,
      uc_currency_format($payment->amount / 100, $sign),
      format_date(strtotime($payment->created_at), 'uc_store'),
      $payment->status,
      format_date(strtotime($payment->charge_date), 'uc_store'),
      //!is_null($payment->paid_at) ? format_date(strtotime($payment->paid_at), 'uc_store') : '--',
    );
  }
  // dpm($payment_rows);
  $form['table'] = array(
    '#theme' => 'table',
    '#header' => $header,
    '#rows' => isset($rows) ? $rows : NULL,
    '#empty' => t('There are no GoCardless payments for this order'),
  );

  if (isset($payment_rows)) {

    $title = t('Payments for @title', array('@title' => $product_select[$ucpid]));
    $form['payment_tables'] = array(
      '#type' => 'fieldset',
      '#title' => $title,
      '#collapsible' => FALSE,
      '#collapsed' => FALSE,
    );
    //$form['payment_tables'][$key]['payments_table'] = array(
    $form['payment_tables']['payments_table'] = array(
      '#theme' => 'table',
      '#header' => $payment_header,
      '#rows' => isset($payment_rows) ? $payment_rows : NULL,
      '#empty' => t('There are no payments for this product yet.'),
    );
  }

  $form['order_id'] = array(
    '#type' => 'value',
    '#value' => $order->order_id,
  );
  $form['mandate'] = array(
    '#type' => 'value',
    '#value' => $mandate,
  );
  $form['product_select'] = array(
    '#type' => 'value',
    '#value' => $product_select,
  );
  $form['create_payment'] = array(
    '#type' => 'fieldset',
    '#title' => t('Create a payment'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#access' => $gc->type == 'P' ? TRUE : FALSE,
  );
  $form['create_payment']['payment_amount'] = array(
    '#type' => 'uc_price',
    '#title' => t('Amount'),
  );
  $form['create_payment']['payment_title'] = array(
    '#type' => 'textfield',
    '#title' => t('Payment title'),
  );
  $form['create_payment']['charge_date'] = array(
    '#title' => t('Charge customer at'),
    '#type' => module_exists('date_popup') ? 'date_popup' : 'date_select',
    '#date_format' => module_exists('date_popup') ? variable_get('date_format_uc_store') : 'd M Y',
    '#default_value' => date('Y-m-d', strtotime($mandate->next_possible_charge_date)),
    '#date_year_range' => '0:+1',
    '#datepicker_options' => array('minDate' => 0),
  );
  $form['create_payment']['payment_submit'] = array(
    '#type' => 'submit',
    '#value' => 'Instruct GoCardless to create a Payment',
    '#validate' => array('uc_gc_client_create_payment_validate'),
    '#submit' => array('uc_gc_client_create_payment_submit'),
  );

  // Generate data for scheduled adjustments table.
  $query = db_select('uc_gc_client_schedules', 's');
  $query->join('uc_gc_client_schedules_adjs', 'a', 'a.sid = s.sid');
  $adjustments = $query
    ->fields('s', array('sid', 'date', 'status'))
    ->fields('a', array('title', 'amount', 'aid'))
    ->condition('s.ucpid', $gc->ucpid, '=')
    ->condition('type', 'adjustment', '=')
    ->orderBy('timestamp', 'ASC')
    ->execute()->fetchAll();

  if (!empty($adjustments)) {
    $adj_rows = array();
    foreach ($adjustments as $adj) {

      $adj->status == 1 ? $cancel = l(t('cancel'), "/gc_client/scheduled_action/cancel/" . $gc->ucid . '/' . $adj->sid . '/' . $adj->aid, $options = array()) : $cancel = NULL;
      $adj->status == 1 ? $spacer = ' | ' : $spacer = NULL;
      $delete = l(t('delete'), "/gc_client/scheduled_action/delete/" . $gc->ucid . '/' . $adj->sid . '/' . $adj->aid, $options = array());
      $actions = $cancel . $spacer . $delete;

      switch ($adj->status) {
        case 0:
          $status = 'Cancelled';
          break;

        case 1:
          $status = 'Pending';
          break;

        case 2:
          $status = 'Complete';
          break;
      }

      $adj_rows[] = array(
        $adj->title,
        $adj->amount,
        $adj->date,
        $status,
        $actions,
      );
    }
  }

  $form['adjust'] = array(
    '#type' => 'fieldset',
    '#title' => t('Schedule adjustments'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t("<p>Make a temporary change to the payment amount that is created with GoCardless.</p>"),
    '#prefix' => t('<div id="adjustments"></div>'),
    '#access' => $gc->type == 'P' ? TRUE : FALSE,
  );

  if (isset($adj_rows)) {

    $form['adjust']['adjust_table'] = array(
      '#type' => 'fieldset',
      '#title' => t('Adjustments'),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );
    $adj_headers = array('Title', 'Adjustment', 'Date', 'Status', 'Actions');
    // Adjustments tables
    // $form['adjust'][$key]['adjs_table'] = array(.
    $form['adjust']['adjust_table']['table'] = array(
      '#theme' => 'table',
      '#header' => $adj_headers,
      '#rows' => isset($adj_rows) ? $adj_rows : NULL,
      '#empty' => t('There are no scheduled adjustments for this product yet.'),
    );
  }
  $form['adjust']['adjust_title'] = array(
    '#type' => 'textfield',
    '#size' => 24,
    '#title' => t('Adjustment title'),
    '#autocomplete_path' => 'gc_client/schedules/autocomplete',
  );
  $form['adjust']['operator'] = array(
    '#type' => 'select',
    '#title' => t('Operation'),
    '#options' => array('add' => 'add', 'subtract' => 'subtract'),
  );
  $form['adjust']['adjustment'] = array(
    '#type' => 'uc_price',
    '#title' => t('Adjustment amount'),
    '#description' => t('This will adjust the amount of bill submitted to GoCardless.'),
  );
  $form['adjust']['payments'] = array(
    '#type' => 'textfield',
    '#size' => 6,
    '#title' => t('Number of payments'),
    '#default_value' => 1,
    '#element_validate' => array('element_validate_positive_number'),
  );
  $form['adjust']['starting_radio'] = array(
    '#type' => 'radios',
    '#title' => t('Starting from'),
    '#options' => array(0 => 'Next billing day', 1 => 'Select another date'),
    '#default_value' => 0,
  );
  $form['adjust']['starting'] = array(
    '#type' => 'date_popup',
    '#title' => t('Starting from'),
    '#description' => t('The adjustment will begin on the first scheduled billing date after that specified here.'),
    '#date_format' => 'd M Y',
    '#date_year_range' => '0:+1',
    '#datepicker_options' => array('minDate' => 0),
    '#states' => array(
      'visible' => array(
        ':input[name="starting_radio"]' => array('value' => 1),
      ),
    ),
  );
  $form['adjust']['plus'] = array(
    '#type' => 'fieldset',
    '#title' => t('and then'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t("Additional scheduled adjustment to follow initial adjustment"),
  );
  $form['adjust']['plus']['plus_operator'] = array(
    '#type' => 'select',
    '#title' => t('Operation'),
    '#options' => array('add' => 'add', 'subtract' => 'subtract'),
  );
  $form['adjust']['plus']['plus_adjustment'] = array(
    '#type' => 'uc_price',
    '#title' => t('Adjustment amount'),
  );
  $form['adjust']['plus']['plus_payments'] = array(
    '#type' => 'textfield',
    '#size' => 6,
    '#title' => t('Number of payments'),
    '#default_value' => 0,
        // '#element_validate' => array('element_validate_positive_number'),.
  );
  $form['adjust']['adjust_button'] = array(
    '#type' => 'submit',
    '#value' => 'Schedule',
    '#validate' => array('uc_gc_client_adjust_validate'),
    '#submit' => array('uc_gc_client_adjust_submit'),
  );

  $form['change_price'] = array(
    '#type' => 'fieldset',
    '#title' => t('Change price'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t("Make a permanent change to the product's price"),
    '#access' => $gc->type == 'P' ? TRUE : FALSE,
  );
  $form['change_price']['cost'] = array(
    '#type' => 'uc_price',
    '#title' => t('New cost'),
      // '#default_value' => $product->cost,.
  );
  $form['change_price']['price'] = array(
    '#type' => 'uc_price',
    '#title' => t('New price'),
      // '#default_value' => $product->price,.
  );
  $form['change_price']['price_button'] = array(
    '#type' => 'submit',
    '#value' => 'Change price',
    '#validate' => array('uc_gc_client_change_price_validate'),
    '#submit' => array('uc_gc_client_change_price_submit'),
  );

  $form['next_payment'] = array(
    '#type' => 'fieldset',
    '#title' => t('Change next scheduled payment creation date'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t('Change the date that the next scheduled payment will be created. (This is not the same date that the customer will be charged on.)'),
    '#access' => $gc->type == 'P' ? TRUE : FALSE,
  );
  // $next_payment = date('D d M Y H:i:s', $gc->next_payment);
  // $form['next_payment']['text'] = array(
  //  '#markup' => t('The next scheduled payment will be created shortly after @next_payment', array('@next_payment' => $next_payment)),
  // );.
  $form['next_payment']['next_payment'] = array(
    '#type' => module_exists('date_popup') ? 'date_popup' : 'date_select',
    '#date_format' => module_exists('date_popup') ? variable_get('date_format_medium') : 'd M Y H:i',
    '#default_value' => date('Y-m-d H:i', $gc->next_payment),
    '#date_year_range' => '0:+1',
    '#datepicker_options' => array('minDate' => 0),
  );
  $form['next_payment']['next_payment_button'] = array(
    '#type' => 'submit',
    '#value' => 'Change date',
      // '#validate' => array('uc_gc_client_next_payment_validate'),.
    '#submit' => array('uc_gc_client_next_payment_submit'),
  );

  return $form;
}

/**
 * AJAX callback function for vegbox_change_form  .
 */
function uc_gc_client_payments_form_callback(&$form, &$form_state) {
  $_SESSION['change_ucpid']['ucpid'] = $form['ucpid']['#value'];
  return $form;
}

/**
 * Custom form validate function.
 */
function uc_gc_client_adjust_validate($form, &$form_state) {

  if ($form_state['values']['adjustment'] == 0) {
    form_set_error('adjustment', "Adjustment cannot be set to zero");
  }
}

/**
 * Custom form submit function.
 */
function uc_gc_client_adjust_submit($form, &$form_state) {

  $mandate = $form_state['values']['mandate'];
  $ucpid = $form_state['values']['ucpid'];
  $order_id = $form_state['values']['order_id'];
  $order = uc_order_load($order_id);
  $product = $order->products[$ucpid];
  $sub = db_select('uc_gc_client', 'u')
    ->fields('u')
    ->condition('ucpid', $ucpid)
    ->execute()->fetch();

  // Prepare data.
  $int_length = $product->data['interval_params']['length'];
  $int_unit = $product->data['interval_params']['unit'];
  $starting = $sub->next_payment;
  if ($form_state['values']['starting_radio'] != 0) {
    $select_date = strtotime($form_state['values']['starting']);
    while ($starting < $select_date) {
      $string = '+' . $product->data['interval_params']['string'];
      $starting = strtotime($string, $starting);
    }
  }

  // Create array containing scheduled dates.
  $payments = $form_state['values']['payments'];
  $dates = array();
  for ($i = 0; $i < $payments; $i++) {
    $string = '+' . ($i * $int_length) . ' ' . $int_unit;
    $dates[] = date('D d M Y', strtotime($string, $starting));
    $ending = strtotime($string, $starting);
  }

  // Make sure sum of order_total and all plus_adjustments for each billdate
  // are not <= 0 and are not > amount remaining.
  $form_state['values']['operator'] == 'add' ? $amount = $form_state['values']['adjustment'] : $amount = 0 - $form_state['values']['adjustment'];

  $warning = uc_gc_client_validate_adjs($order, $ucpid, $dates, $mandate, $amount);
  if ($warning) {
    return;
  }

  if ($form_state['values']['plus_adjustment'] != 0) {

    // Create array containing additional scheduled dates.
    $plus_starting = strtotime('+' . $int_length . ' ' . $int_unit, $ending);
    $plus_payments = (int) $form_state['values']['plus_payments'];
    $plus_dates = array();
    for ($i = 0; $i < $plus_payments; $i++) {
      $string = '+' . ($i * $int_length) . ' ' . $int_unit;
      $plus_dates[] = date('D d M Y', strtotime($string, $plus_starting));
    }

    // Validate plus_adjustment.
    $form_state['values']['plus_operator'] == 'add' ? $plus_amount = $form_state['values']['plus_adjustment'] : $plus_amount = 0 - $form_state['values']['plus_adjustment'];
    $plus_warning = uc_gc_client_validate_adjs($order, $ucpid, $plus_dates, $mandate, $plus_amount);
    if ($plus_warning) {
      return;
    }
  }

  $_SESSION['adjs'] = array();

  // Add schedules data to database.
  foreach ($dates as $date) {

    $insert = db_insert('uc_gc_client_schedules')
      ->fields(array(
        'ucid' => $order_id,
        'ucpid' => $ucpid,
        'type' => 'adjustment',
        'date' => $date,
        'timestamp' => strtotime($date),
        'status' => 1,
        'created' => REQUEST_TIME,
      ))
      ->execute();

    $_SESSION['adjs'][$insert] = $date;

    $insert_dates = db_insert('uc_gc_client_schedules_adjs')
      ->fields(array(
        'sid' => $insert,
        'title' => !empty($form_state['values']['adjust_title']) ? $form_state['values']['adjust_title'] : 'Adjustment',
        'amount' => $amount,
      ))
      ->execute();
  }

  if ($form_state['values']['plus_adjustment'] != 0) {

    foreach ($plus_dates as $plus_date) {

      $plus_insert = db_insert('uc_gc_client_schedules')
        ->fields(array(
          'ucid' => $order_id,
          'ucpid' => $ucpid,
          'type' => 'adjustment',
          'date' => $plus_date,
          'timestamp' => strtotime($plus_date),
          'status' => 1,
          'created' => REQUEST_TIME,
        ))
        ->execute();

      $plus_insert_dates = db_insert('uc_gc_client_schedules_adjs')
        ->fields(array(
          'sid' => $plus_insert,
          'title' => $form_state['values']['adjust_title'],
          'amount' => $plus_amount,
        ))
        ->execute();
    }
  }
  drupal_set_message(t('New Scheduled Adjustment successfully added'));
}

/**
 * Custom validation function: checks integrity of scheduled adjustments.
 */
function uc_gc_client_validate_adjs($order, $ucpid, $dates, $mandate, $amount) {

  foreach ($dates as $date) {

    // Calculate sum of scheduled adjs for date.
    $db_query = "SELECT s.ucid, s.date, sum( a.amount ) AS sum FROM {uc_gc_client_schedules} s inner join {uc_gc_client_schedules_adjs} a on s.sid = a.sid WHERE s.status = 1 AND s.date = '" . $date . "' AND s.ucpid = " . $order->order_id;
    $result = db_query($db_query)
      ->fetch();
    is_null($result->sum) ? $sum = 0 : $sum = $result->sum;

    // Provide a hook so that $amount and/or $price can be altered by another
    // module.
    $price = $order->order_total;
    drupal_alter('adjs_validate', $amount, $price, $date, $order->order_id);
    $sum = $sum + $amount + $price;

    if ($sum < 1 && $sum != 0) {
      drupal_set_message(t('The schedule cannot be placed because the sum of scheduled adjustments, plus the price of the product, for @date is less than @amount, which is not allowed by GoCardless', array('@date' => $date, '@amount' => uc_currency_format(1))), 'warning');
      $warning = TRUE;
    }

    /*
    $next_int = date('D d M Y', strtotime($preauth->next_interval_start));

    //TODO CHECK THIS
    if ($date < $next_int) {
    $remaining = $preauth->remaining_amount;
    if ($sum > $remaining) {
    drupal_set_message(t('The schedule cannot be placed because the sum of scheduled adjustments, plus the price of the product, for @date exceeds the amount remaining for the current period', array('@date' => $date, )),'warning');
    $warning = TRUE;
    }
    } else {
    $max_amount = $preauth->max_amount;
    if ($sum > $max_amount) {
    drupal_set_message(t('The schedule cannot be placed because the sum of scheduled adjustments, plus the price of the product for @date exceeds the maximum amount for that period', array('@date' => $date, )),'warning');
    $warning = TRUE;
    }
    }
     */
    if ($warning) {
      return $warning;
    }
  }
}

/**
 * Custom form validate function.
 */
function uc_gc_client_create_payment_validate($form, &$form_state) {

  $mandate = $form_state['values']['mandate'];
  $amount = $form_state['values']['payment_amount'];
  // If (empty($form_state['values']['payment_product_id']))
  //  form_set_error('payment_product_id', 'You must select a product when raising a bill');.
  if ($amount == 0) {
    form_set_error('payment_amount', 'Please provide an amount');
  }
  elseif ($amount < 1) {
    form_set_error('payment_amount', t('The minimum bill amount for GoCardless id @amount', array('@amount' => uc_currency_format(1))));
  }

  // Check that specified date is greater than or equal to the
  // next possible charge date.
  if (!is_null($form_state['values']['charge_date'])) {
    if (strtotime($form_state['values']['charge_date']) < strtotime($mandate->next_possible_charge_date)) {
      form_set_error('charge_date', 'The date cannot be before the Next Possible Charge Date.');
    }
  }
}

/**
 * Custom form submit function.
 */
function uc_gc_client_create_payment_submit($form, &$form_state) {

  $mandate = $form_state['values']['mandate'];
  $amount = $form_state['values']['payment_amount'];
  $order_id = $form_state['values']['order_id'];
  $order = uc_order_load($order_id);
  $ucpid = $form_state['values']['ucpid'];
  // $ucpid = $form_state['values']['payment_product_id'];.
  $params = array(
    'endpoint' => 'payments',
    'action' => 'create',
    'mandate' => $mandate->id,
    'amount' => $amount,
    'currency' => variable_get('uc_currency_code'),
    'description' => !empty($form_state['values']['payment_title']) ? $form_state['values']['payment_title'] : $order->products[$ucpid]->title,
    'charge_date' => $form_state['values']['charge_date'],
    'metadata' => array(
      'ucpid' => $ucpid,
    ),
  );
  $result = uc_gc_client_api($params);
  $payment = $result->response->body->payments;

  $text = t('A one-off payment for @amount has been created by Admin with GoCardless mandate @mandate.', array('@amount' => uc_currency_format($amount), '@mandate' => $mandate->id));
  drupal_set_message(t('@text', array('@text' => $text)));
  uc_order_comment_save($order->order_id, $order->uid, $text, 'order');
  $watchdog = t('Order #@order_id:', array('@order_id' => $order->order_id)) . ' ' . $text;
  watchdog('uc_gc_client', $watchdog, array(), WATCHDOG_INFO);

  // Provide a hook so that other modules can respond to payment creation.
  module_invoke_all('payment_created', $payment, $order->order_id);     
}

/**
 * Custom form submit function.
 */
function uc_gc_client_next_payment_submit($form, &$form_state) {

  $next_payment = strtotime($form_state['values']['next_payment']);
  $ucpid = $form_state['values']['ucpid'];
  $title = $form_state['values']['product_select'][$ucpid];

  $db_update = db_update('uc_gc_client')
    ->fields(array(
      'next_payment' => $next_payment,
    ))
    ->condition('ucpid', $ucpid, '=')
    ->execute();

  drupal_set_message(t('The next payment creation date for @title has been updated to @next_payment', array('@title' => $title, '@next_payment' => format_date($next_payment, 'medium'))));
}

/**
 * Custom form validate function.
 */
function uc_gc_client_change_price_validate($form, &$form_state) {
}

/**
 * Custom form submit function.
 */
function uc_gc_client_change_price_submit($form, &$form_state) {

  $cost = $form_state['values']['cost'];
  $price = $form_state['values']['price'];
  $order_id = $form_state['values']['order_id'];
  $ucpid = $form_state['values']['ucpid'];
  $order = uc_order_load($order_id);

  $gc = db_select('uc_gc_client', 'u')
    ->fields('u', array('ucpid', 'next_payment'))
    ->condition('ucpid', $ucpid)
    ->execute()->fetch();

  $price_old = uc_currency_format($order->products[$gc->ucpid]->price);
  $price_new = uc_currency_format($price);
  $order->products[$gc->ucpid]->cost = $cost;
  $order->products[$gc->ucpid]->price = $price;
  $title = $order->products[$gc->ucpid]->title;
  $next = format_date($gc->next_payment, 'uc_store');

  uc_order_save($order);

  $text = t('The price for @title has been changed permanently from @price_old to @price_new. The next payment is scheduled for @next.', array('@title' => $title, '@price_old' => $price_old, '@price_new' => $price_new, '@next' => $next));
  drupal_set_message(t('@text', array('@text' => $text)));
  uc_order_comment_save($order->order_id, $order->uid, $text, 'order');
  $watchdog = t('Order #@order_id:', array('@order_id' => $order->order_id)) . ' ' . $text;
  watchdog('uc_gc_client', $watchdog, array(), WATCHDOG_INFO);
}

/**
 * Page callback function.
 */
function uc_gc_client_scheduled_action($action, $order_id, $sid, $aid) {

  if ($action == 'cancel') {
    $update = db_update('uc_gc_client_schedules')
      ->fields(array(
        'status' => 0,
      ))
      ->condition('sid', $sid, '=')
      ->execute();
  }

  if ($action == 'delete') {

    $delete_sch = db_delete('uc_gc_client_schedules')
      ->condition('sid', $sid, '=')
      ->execute();
    $delete_adj = db_delete('uc_gc_client_schedules_adjs')
      ->condition('aid', $aid, '=')
      ->execute();
  }
  $action == 'cancel' ? $action = 'cancelled' : $action = 'deleted';
  drupal_set_message(t('Scheduled adjustment @action.', array('@action' => $action)));
  $path = 'admin/store/orders/' . $order_id . '/gocardless';
  drupal_goto($path, array('fragment' => 'adjustments'));
}

/**
 * Menu callback function.
 */
function uc_gc_client_schedules_autocomplete($string) {

  $matches = array();
  $titles = explode("\n", variable_get('uc_gc_client_preauth_adjs'));
  foreach ($titles as $title) {
    if (strpos(strtolower($title), strtolower($string)) !== FALSE) {
      $matches[$title] = check_plain($title);
    }
  }
  drupal_json_output($matches);
}