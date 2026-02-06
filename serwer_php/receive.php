<?php
session_start();

$usersFile = "users.json";
$timeout = 15; // po ilu sekundach uznajemy usera za offline

// unikalny identyfikator użytkownika
if (!isset($_SESSION['user_id'])) {
    $_SESSION['user_id'] = uniqid("user_");
}
$userId = $_SESSION['user_id'];

// wczytaj listę użytkowników
$users = file_exists($usersFile)
    ? json_decode(file_get_contents($usersFile), true)
    : [];

// zaktualizuj czas ostatniej aktywności
$users[$userId] = time();

// usuń nieaktywnych
foreach ($users as $id => $lastSeen) {
    if (time() - $lastSeen > $timeout) {
        unset($users[$id]);
    }
}

// zapisz z powrotem
file_put_contents($usersFile, json_encode($users));

// odpowiedź
header("Content-Type: application/json");
echo json_encode([
    "online" => array_keys($users),
    "count" => count($users)
]);
