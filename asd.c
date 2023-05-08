int make_children (void) {
  int i = 0; // 자식 프로세스의 깊이를 나타내는 변수 i를 초기화
  int pid; // fork() 함수의 반환값을 저장할 변수 pid 선언
  char child_name[128]; // 자식 프로세스의 이름을 저장할 문자열 변수 child_name 선언

  // 무한 반복문
  for (; ; random_init (i), i++) {
    if (i > EXPECTED_DEPTH_TO_PASS/2) { // 깊이가 EXPECTED_DEPTH_TO_PASS/2보다 크면
      snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "X"); // 자식 프로세스의 이름을 "child_i_X"로 설정
      pid = fork(child_name); // 자식 프로세스 생성
      if (pid > 0 && wait (pid) != -1) { // 부모 프로세스이면서 자식 프로세스가 정상적으로 종료되었을 때
        fail ("crashed child should return -1."); // 오류 메시지 출력
      } else if (pid == 0) { // 자식 프로세스이면
        consume_some_resources_and_die(); // 자원을 소모한 후 비정상적으로 종료
        fail ("Unreachable"); // 이 코드는 실행되지 않을 것이므로 오류 메시지 출력
      }
    }

    snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "O"); // 자식 프로세스의 이름을 "child_i_O"로 설정
    pid = fork(child_name); // 자식 프로세스 생성
    if (pid < 0) { // 자식 프로세스 생성에 실패한 경우
      exit (i); // 자식 프로세스를 생성하지 못했으므로 i를 반환하며 종료
    } else if (pid == 0) { // 자식 프로세스이면
      consume_some_resources(); // 자원을 소모한 후 정상적으로 종료
    } else { // 부모 프로세스이면
      break; // 반복문을 빠져나옴
    }
  }

  int depth = wait (pid); // 마지막 자식 프로세스가 종료될 때까지 기다림
  if (depth < 0) // 자식 프로세스가 비정상적으로 종료되었을 경우
	  fail ("Should return > 0."); // 오류 메시지 출력

  if (i == 0) // 깊이가 0인 경우
	  return depth; // 마지막 자식 프로세스의 종료값을 반환
  else
	  exit (depth); // 마지막 자식 프로세스의 종료값을 반환하며 종료
}
