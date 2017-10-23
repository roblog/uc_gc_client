<?php

/**
 * @file
 * Extra tools for managing GoCardless orders and mandates.
 */

/**
 * Implements hook_form.
 */
function uc_gc_client_priceshift_form($form, $form_state) {

  $sandbox = variable_get('uc_gc_client_sandbox');
  $subs = db_select('uc_gc_client', 'g')
    ->fields('g')
    ->condition('sandbox', $sandbox)
    ->condition('type', 'P')
    ->condition('status', 'canceled')
    ->orderBy('ucid', 'DESC')
    ->execute()
    ->fetchAll();

  $headers = array('Order ID', 'Product', "Product's Store cost", "Product's Store price", 'Attributes (cost | price)', 'Actual cost', 'Actual price');
  $rows = array();
  $product_select = array();

  foreach ($subs as $sub) {

    $order = uc_order_load($sub->ucid);
    $product = $order->products[$sub->ucpid];
    $order_path = 'admin/store/orders/' . $sub->ucid;

    $uc_product = db_select('uc_products', 'p')
      ->fields('p', array('cost', 'sell_price'))
      ->condition('nid', $product->nid, '=')
      ->execute()->fetch();

    // Create attribute data.
    $attributes = $product->data['attributes'];
    $attr_row = '';
    foreach ($attributes as $attr_key => $attribute) {
      foreach ($attribute as $option_key => $option_value) {
        $option_array = db_select('uc_attribute_options', 'o')
          ->fields('o', array('cost', 'price'))
          ->condition('oid', $option_key, '=')
          ->execute()->fetchAssoc();
        if ($option_array['cost'] > 0 || $option_array['price'] > 0) {
          $attr_cost = uc_currency_format($option_array['cost']);
          $attr_price = uc_currency_format($option_array['price']);

          $line = t('@attr_key->@option_value: @attr_cost | @attr_price<br>', array('@attr_key' => $attr_key, '@option_value' => $option_value, '@attr_cost' => $attr_cost, '@attr_price' => $attr_price));
          $attr_row .= $line;
        }
      }
    }

    $rows[$sub->ucpid] = array(
      l($sub->ucid, $order_path),
      $product->title,
      uc_currency_format($uc_product->cost),
      uc_currency_format($uc_product->sell_price),
      $attr_row,
      uc_currency_format($product->cost),
      uc_currency_format($product->price),
    );

    if (!array_key_exists($product->title, $product_select)) {
      $product_select[$product->title] = $product->title;
    }

  }

  $form['text'] = array(
    '#markup' => t('<p><h4>Update cost and price for all products of a particular type, or by selecting particular orders from the list below.</h4></p>'),
  );
  $form['bulk_change'] = array(
    '#type' => 'checkbox',
    '#prefix' => t('<b>Bulk change</b>'),
    '#default_value' => 0,
    '#title' => t("Bulk change all products of same type."),
  );
  $form['product'] = array(
    '#type' => 'select',
    '#title' => 'Select product for updating price',
    '#options' => $product_select,
    '#empty_option' => t('- Select -'),
    '#states' => array(
      'visible' => array(
        ':input[name="bulk_change"]' => array('checked' => TRUE),
      ),
    ),
  );
  $form['operator'] = array(
    '#type' => 'select',
    '#title' => 'Select operation',
    '#options' => array('increase' => 'increase', 'decrease' => 'decrease'),
    '#empty_option' => t('- Select -'),
  );
  $form['cost'] = array(
    '#type' => 'uc_price',
    '#title' => t('Adjust cost'),
    '#description' => t('Amount to increase / decrease cost by'),
  );
  $form['price'] = array(
    '#type' => 'uc_price',
    '#title' => t('Adjust price'),
    '#description' => t('Amount to increase / decrease price by'),
  );
  $form['submit_top'] = array(
    '#type' => 'submit',
    '#value' => t("Update"),
  );
  $form['table'] = array(
    '#type' => 'tableselect',
    '#header' => $headers,
    '#options' => isset($rows) ? $rows : NULL,
    '#empty' => t('No data'),
  );
  $form['subs'] = array(
    '#type' => 'value',
    '#value' => $subs,
  );
  $form['submit_bottom'] = array(
    '#type' => 'submit',
    '#value' => t("Update"),
  );
  return $form;
}

/**
 * Implements hook_form_validate.
 */
function uc_gc_client_priceshift_form_validate($form, $form_state) {

  if (empty($form_state['values']['operator'])) {
    form_set_error('operator', t('Please specify operation'));
  }
}

/**
 * Implements hook_form_submit.
 */
function uc_gc_client_priceshift_form_submit($form, $form_state) {

  $operator = $form_state['values']['operator'];
  if ($operator == 'increase') {
    $cost_adj = $form_state['values']['cost'];
    $price_adj = $form_state['values']['price'];
  }
  else {
    $cost_adj = 0 - $form_state['values']['cost'];
    $price_adj = 0 - $form_state['values']['price'];
    $price_adj = number_format((float) 0 - $form_state['values']['price'], 2, '.', '');
  }

  if (!empty($form_state['values']['product'])) {

    $product_title = $form_state['values']['product'];
    $subs = $form_state['values']['subs'];

    foreach ($subs as $sub) {
      $order = uc_order_load($sub->ucid);
      $product = &$order->products[$sub->ucpid];
      if ($product->title == $product_title) {

        $cost_old = uc_currency_format($product->cost);
        $price_old = uc_currency_format($product->price);
        $product->cost = $product->cost + $cost_adj;
        $product->price = $product->price + $price_adj;
        uc_order_save($order);

        $message = t("Order #@order_id with product: '@product', has had its cost changed from @cost_old to @cost, and its price changed from @price_old to @price.", array('@order_id' => $sub->ucid, '@product' => $product->title, '@cost_old' => $cost_old, '@cost' => uc_currency_format($product->cost), '@price_old' => $price_old, '@price' => uc_currency_format($product->price)));
        // $count++;.
        drupal_set_message($message);

        $comment = t("Product '@product' has had its price changed from @price_old to @price.", array('@product' => $product_title, '@price_old' => $price_old, '@price' => uc_currency_format($product->price)));
        uc_order_comment_save($sub->ucid, $order->uid, $comment, 'order');
      }
    }
  }

  foreach ($form_state['values']['table'] as $ucpid) {
    if (!empty($ucpid)) {

      $ucid = db_select('uc_gc_client', 'g')
        ->fields('g', array('ucid'))
        ->condition('ucpid', $ucpid)
        ->execute()
        ->fetchField();

      $order = uc_order_load($ucid);
      $product = &$order->products[$ucpid];
      $cost_old = uc_currency_format($product->cost);
      $price_old = uc_currency_format($product->price);
      $product->cost = $product->cost + $cost_adj;
      $product->price = $product->price + $price_adj;
      uc_order_save($order);
      $message = t("Order #@order_id with product: '@product', has had its cost changed from @cost_old to @cost, and its price changed from @price_old to @price.", array('@order_id' => $ucid, '@product' => $product->title, '@cost_old' => $cost_old, '@cost' => uc_currency_format($product->cost), '@price_old' => $price_old, '@price' => uc_currency_format($product->price)));
      drupal_set_message($message);

      $comment = t("Product '@product' has had its price changed from @price_old to @price.", array('@product' => $product->title, '@price_old' => $price_old, '@price' => $product->price));
      uc_order_comment_save($order->order_id, $order->uid, $comment, 'order');
    }
  }
}