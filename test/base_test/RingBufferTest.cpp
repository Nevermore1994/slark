//
// Created by Nevermore on 2024/4/12.
// slark RingBufferTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include <chrono>
#include "RingBuffer.hpp"

using namespace slark;

TEST(RingBuffer, basic) {
    RingBuffer<int, 10> buffer;
    int data[] = {1, 2, 3, 4, 5};
    buffer.append(data, 5);

    ASSERT_EQ(buffer.length(), 5);

    int readData[5] = {0};
    buffer.read(readData, 5);

    ASSERT_EQ(std::memcmp(data, readData, 5 * sizeof(int)), 0);
    ASSERT_TRUE(buffer.isEmpty());
}

TEST(RingBuffer, wrapAround) {
    RingBuffer<int, 10> buffer;
    int data1[] = {1, 2, 3, 4, 5};
    buffer.append(data1, 5);

    int data2[] = {6, 7, 8, 9, 10};
    buffer.append(data2, 5);

    ASSERT_TRUE(buffer.length() == 10);

    int readData[10] = {0};
    buffer.read(readData, 10);

    int expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    ASSERT_TRUE(std::memcmp(readData, expected, 10 * sizeof(int)) == 0);
    ASSERT_TRUE(buffer.isEmpty());
}

TEST(RingBuffer, partialRead) {
    RingBuffer<int, 10> buffer;
    int data[] = {1, 2, 3, 4, 5};
    buffer.append(data, 5);

    int readData[3] = {0};
    buffer.read(readData, 3);

    int expected[] = {1, 2, 3};
    ASSERT_TRUE(std::memcmp(readData, expected, 3 * sizeof(int)) == 0);
    ASSERT_TRUE(buffer.length() == 2);
}

TEST(RingBuffer, reset) {
    RingBuffer<int, 10> buffer;
    int data[] = {1, 2, 3, 4, 5};
    buffer.append(data, 5);

    buffer.reset();
    ASSERT_TRUE(buffer.isEmpty());
    ASSERT_TRUE(buffer.length() == 0);

    int readData[5] = {0};
    ASSERT_TRUE(buffer.read(readData, 5) == 0);  // Reading from empty buffer
}

TEST(RingBuffer, isFull) {
    RingBuffer<int, 5> buffer;
    int data[] = {1, 2, 3, 4, 5};
    buffer.append(data, 5);

    ASSERT_TRUE(buffer.isFull());
    ASSERT_TRUE(buffer.length() == 5);

    int readData[5] = {0};
    buffer.read(readData, 5);
    ASSERT_TRUE(buffer.isEmpty());
}

TEST(RingBuffer, length) {
    RingBuffer<char> buffer;
    ASSERT_EQ(buffer.isEmpty(), true);
    ASSERT_EQ(buffer.length(), 0ull);
    std::string t = "hello world";
    buffer.append(t.c_str(), t.length());
    ASSERT_EQ(buffer.length(), t.length());

    {
        RingBuffer<char, 10> buffer;
        std::string t = "hello world";
        buffer.append(t.c_str(), t.length());
        ASSERT_EQ(buffer.length(), 10);
    }
}

TEST(RingBuffer, read) {
    RingBuffer<char> buffer;
    std::string t = "hello world";
    buffer.append(t.c_str(), t.length());
    auto p = new char[5];
    auto length =  buffer.read(p, 5);
    ASSERT_STREQ(p, "hello");
    delete[] p;
    ASSERT_EQ(length, 5);
    ASSERT_EQ(buffer.length(), t.length() - 5);

    p = new char[20];
    memset(p, 0, 20);
    length =  buffer.read(p, 20);
    ASSERT_STREQ(p, " world");
    ASSERT_EQ(length, 6);
    ASSERT_EQ(buffer.length(), 0);
    ASSERT_EQ(buffer.isEmpty(), true);
}

TEST(RingBuffer, longStr) {
    RingBuffer<char, 1024> buffer;
    std::string t = "Lorem ipsum dolor sit amet. Vel sapiente voluptates et magni veritatis aut velit deleniti? Ex beatae illo ut sint aperiam non adipisci similique est beatae voluptate id minus omnis est nemo eligendi. Eum omnis itaque sed dolores quae qui reiciendis excepturi! Et repellendus error sed iusto quod est accusamus blanditiis quo tempora cupiditate! Non voluptate nemo qui voluptates voluptatem qui dolorum reprehenderit. Sit aliquid distinctio ut vero dolores ea dolorum quia. Et dolor facilis quo possimus ipsa ut velit laudantium a deserunt placeat ab voluptates accusamus sed repudiandae similique. Qui quia rerum ad molestiae incidunt qui veniam asperiores. Qui mollitia recusandae ut porro facilis et sint asperiores ex modi sunt et nulla omnis aut nihil explicabo est suscipit quisquam. In pariatur omnis est Quis quaerat qui quia adipisci est dolores dignissimos ad expedita cupiditate. Et minus soluta hic voluptate harum et quidem cupiditate eos atque eius. Et officiis iste rem suscipit quasi At voluptatem dolore eum vero autem ut modi praesentium aut nulla incidunt. Ut corporis Quis aut voluptatem molestias qui debitis molestiae ut natus quas cum velit quam ex blanditiis quia? Et laudantium corrupti sit quos soluta et incidunt ullam! </p><p>Sit voluptatum rerum ea veritatis laudantium aut eligendi consequatur eum totam praesentium eum nostrum consequatur qui unde iure ad voluptatem quisquam. Et temporibus veniam a consequuntur adipisci et quam omnis sit consequatur fuga. Ut eaque adipisci hic labore dolor et voluptate cupiditate. Ut impedit obcaecati cum soluta perspiciatis et autem laboriosam id maxime nesciunt. Et quae velit ut porro possimus ut sunt dolor id libero dolores non quasi cumque. 33 earum quibusdam aut perferendis dignissimos eum voluptatem inventore. Id accusamus similique vel rerum iure ut ipsam veritatis. Eos quaerat consequatur ut explicabo beatae est voluptatum commodi. Non beatae molestias est delectus iusto eum deleniti itaque? Et quisquam accusantium est laboriosam quisquam ut consequatur minima et quis consequuntur et quia sint sit dignissimos minus est nemo architecto. Qui molestiae possimus est molestiae accusamus aut distinctio aliquid et iusto mollitia et dolor voluptas in minima eius non delectus amet! Sed consequatur autem sed ratione voluptates ut earum quae vel autem dolores aut reiciendis asperiores. Et consectetur excepturi et blanditiis nemo et deleniti galisum et accusantium voluptatem ab impedit quia ut consectetur quia qui quasi nobis. Et nemo quidem 33 dolorem asperiores in tempora fugit est quaerat officiis. </p><p>Id quia tenetur aut consequatur illum ut tempora accusantium in corrupti provident est omnis eius est error eaque est molestiae accusantium. Sit deserunt perferendis ut accusamus deserunt ut fugit veritatis quo culpa vero ea dolores omnis. Ut explicabo similique sit quisquam alias et minus enim ea magni perspiciatis sit temporibus vero eum impedit enim. Cum cupiditate voluptatem et debitis vitae aut cumque nobis aut Quis laborum cum ipsa fugit. Sed nostrum dolorum est voluptatem sapiente ea itaque ducimus vel ipsum asperiores? Ut assumenda sequi in nisi facilis qui magnam nemo qui adipisci accusamus sed voluptates dolor et delectus alias in omnis soluta. Aut quia reiciendis et mollitia aliquid eos expedita reiciendis ut dicta molestiae est accusantium veritatis. Qui quae cumque ad deleniti quas ex rerum ullam nam unde reprehenderit et omnis unde et libero tempora aut voluptatem autem. Ut adipisci pariatur sit vero voluptatibus ut voluptatum consectetur aut molestias voluptatem et assumenda nihil. Ex neque ducimus ut aspernatur aspernatur in voluptas deleniti eum explicabo sequi eos velit enim aut aperiam facilis ut commodi quisquam. Sit harum saepe non illo quia est quia nihil ab officia enim et libero error. Aut tempore quas est temporibus nulla ea voluptatibus harum ea omnis veniam sed repellat temporibus. Aut dolor aperiam et fuga vitae et quas tempora aut facere autem aut placeat corrupti et possimus eius ea explicabo magnam. Et adipisci cumque ex inventore maiores aut fugiat inventore.";
    buffer.append(t.c_str(), t.length());
    ASSERT_EQ(buffer.length(), 1024);

}

