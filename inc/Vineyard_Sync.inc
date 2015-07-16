<?php

class Vineyard_Sync
{
  public $trellises = array();
  public $entities = array();
  public $server;

  static $instance;

  static function get_instance()
  {
    if (!Vineyard_Sync::$instance)
      Vineyard_Sync::$instance = new Vineyard_Sync();

    return Vineyard_Sync::$instance;
  }

  static function load_json_from_file($filename)
  {
    $json = file_get_contents($filename);
    if ($json === false)
      throw new Exception('Could not find file: ' . $filename);

    $data = json_decode($json);
    if (!is_object($data)) {
      throw new Exception("Invalid JSON in file $filename.");
    }

    return $data;
  }

  function __construct()
  {
    $path = str_replace("\\", '/', realpath(".")) . '/sites/all/config/vineyard-sync.json';
//    print "path='$path'";
    if (file_exists($path))
      $this->add_settings_file($path);
  }

  function add_settings_file($filename)
  {
    $data = Vineyard_Sync::load_json_from_file($filename);
//    print_r($data);
    foreach ($data->entities as $name => $entity) {
      $this->entities[$name] = $entity;
    }

    foreach ($data->trellises as $name => $trellis) {
      $this->trellises[$name] = $trellis;
    }

    if ($data->server) {
      $this->server = $data->server;
    }
  }

  private function login()
  {
    print cli_debug("tmp: " . sys_get_temp_dir() . "\n");
    $url = $this->server->url . '/vineyard/login';
    return $this->post($url, json_encode($this->server->user));
  }

  function send($entity, $category, $deleted = false)
  {
    $type = Vineyard_Sync::get_type($entity, $category);

    if (!isset($this->entities[$type]))
      return;

    if (!$this->server) {
      throw new Exception('Cannot send update.  Server connection was not configured.');
    }

    $data = array(
      'objects' => array($this->package_entity($entity, $type))
    );
    if ($deleted)
      $data['objects'][0]->_deleted = true;

    $data_string = json_encode($data);

    $status = $this->send_update($data_string);
    if ($status == 401) {
      $this->login();
      $this->send_update($data_string);
    }
  }

  private function post($url, $data)
  {
    cli_debug(print "sending update to $url\n");
    cli_debug(print $data . "\n");
    $cookie_file = drupal_realpath(file_default_scheme() . '://') . PATH_SEPARATOR . "SYNC_COOKIE.txt";
//    $cookie_file = 'E:/Websites/tmp/cookie.tmp';
    cli_debug(print "COOKIE FILE: $cookie_file\n");

    $handle = curl_init($url);
    curl_setopt($handle, CURLOPT_CUSTOMREQUEST, "POST");
//    curl_setopt($handle, CURLOPT_HEADER, 1);
    curl_setopt($handle, CURLOPT_POSTFIELDS, $data);
    curl_setopt($handle, CURLOPT_RETURNTRANSFER, true);
//    curl_setopt($handle, CURLOPT_COOKIESESSION, true);
    curl_setopt($handle, CURLOPT_COOKIEJAR, $cookie_file);
    curl_setopt($handle, CURLOPT_COOKIEFILE, $cookie_file);

    curl_setopt($handle, CURLOPT_HTTPHEADER, array(
        'Content-Type: application/json',
        'Content-Length: ' . strlen($data))
    );

    $content = curl_exec($handle);
    $error = curl_error($handle);
    if ($error)
      cli_debug("cURL Error: $error");

    $httpCode = curl_getinfo($handle, CURLINFO_HTTP_CODE);
    cli_debug("$httpCode - $content\n");
    curl_close($handle);

    return $httpCode;
  }

  private function send_update($data_string)
  {
    $url = $this->server->url . '/vineyard/update';
    return $this->post($url, $data_string);
  }

  private function package_entity($source, $type)
  {
    $map = $this->entities[$type];
    $object = $this->populate_package($source, $map);
    $object->trellis = $map->name ? $map->name : $type;
    return $object;
  }

  private function populate_package($source, $map, $object = null)
  {
    if (!$object) {
      if ($map->parent) {
        $object = $this->populate_package($source, $this->entities[$map->parent]);
      } else {
        $object = new stdClass();
      }
    }

    foreach ($map->properties as $key => $property) {
      $target_key = isset($property->name) ? $property->name : $key;
      $value = $source->{$key};

      if (is_object($value) || is_array($value)) {
        if (isset($property->unbox) && $property->unbox) {
          for ($i = 0; $i < $property->unbox; ++$i) {
            $value = reset($value);
          }
        }

        if (isset($property->properties)) {
          $this->populate_package((object)$value, $property, $object);
          continue;
        }

        if (isset($this->entities[$property->trellis])) {
          if ($property->type == 'list') {
            $list = $value;
            $value = array();
            foreach ($list as $item) {
              $value[] = $this->populate_package((object)$item, $this->entities[$property->trellis]);
            }
          } else {
            $value = $this->populate_package((object)$value, $this->entities[$property->trellis]);
          }
        }

        if (isset($property->grab) && $property->grab) {
          $value = (object)$value;
          $value = $value->{$property->grab};
        }
      }

      if ($property->type == 'base64')
        $value = base64_encode($value);
      else if ($property->type == 'bool')
        $value = (bool)intval($value);

      $object->{$target_key} = $value;
    }

    return $object;
  }

  static function get_type($entity, $category)
  {
    if ($category == 'node')
      return $entity->type;

    return $category;
  }

}


function cli_debug($message)
{
  if (php_sapi_name() == "cli") {
    print $message;
  }
}