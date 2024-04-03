/**
*Arturo Argueta 21527
*Daniel EStrada 
*/
#include <chrono>
#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include "protocol.pb.h"
using namespace std;
// to compile:
// g++ server.cpp protocol.pb.cc -o serverx -lprotobuf -lpthread

//estructura para modelar al cliente y facilitar el manejo de su información
struct Cli{
    int socket;
    string username;
    char ip[INET_ADDRSTRLEN]; //16 bits
    string status;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastActivityTime; // Nuevo campo para almacenar el tiempo de la última actividad
};

//all the clients en un "diccionario"
unordered_map<string,Cli*> servingCLients;

/**
 * Devuelve error al socket indicado
 * socketId: int -> del socket a decvolver 
 * errorMessage: stirng -> mensaje de error a devolver
 * optionHandler: int -> el tipo de request que se estaba realizando
 * >1: Registro de Usuarios
 * >2: Usuarios Conectados
 * >3: Cambio de Estado
 * >4: Mensajes
 * >5: Informacion de un usuario en particular
*/
void ErrorResponse(int optionHandled , int socketID , string errorMessage){
    char buff[8192];
    chat::ServerResponse *errorRes = new chat::ServerResponse();
    string msSerialized;
    errorRes->set_option(optionHandled);
    errorRes->set_code(500);
    errorRes->set_servermessage(errorMessage);
    //calcular tamaño del buffer a emplear
    errorRes->SerializeToString(&msSerialized);
    strcpy(buff, msSerialized.c_str());
    if(!send(socketID, buff, msSerialized.size() + 1, 0)){cout<<"E: HANDLER FAILED"<<endl;};
}

void handleUserRegistration(int socket, const chat::ClientPetition& request, Cli& client, Cli& newClient) {
    std::cout << std::endl << "__RECEIVED INFO__\nUsername: " << request.registration().username() << "\t\tip: " << request.registration().ip();
    if (servingCLients.count(request.registration().username()) > 0) {
        std::cout << std::endl << "ERROR: Username already exists" << std::endl;
        ErrorResponse(1, socket, "ERROR: Username already exists");
        return;
    }

    // Crear la respuesta del servidor
    chat::ServerResponse response;
    response.set_option(1);
    response.set_servermessage("SUCCESS: register");
    response.set_code(200);

    // Serializar y enviar la respuesta
    string msgServer;
    response.SerializeToString(&msgServer);
    char buffer[8192];
    strcpy(buffer, msgServer.c_str());
    send(socket, buffer, msgServer.size() + 1, 0);

    std::cout << std::endl << "SUCCESS: The user " << request.registration().username() << " was added with the socket: " << socket << std::endl;

    // Actualizar la información del cliente
    client.username = request.registration().username();
    client.socket = socket;
    client.status = "activo";
    strcpy(client.ip, newClient.ip);
    servingCLients[client.username] = &client;
    servingCLients[client.username]->lastActivityTime = std::chrono::high_resolution_clock::now();
    servingCLients[client.username]->status = "activo";
}

void handleUserQuery(int socket, const chat::ClientPetition& request, Cli& client) {
    servingCLients[client.username]->lastActivityTime = std::chrono::high_resolution_clock::now();
    servingCLients[client.username]->status = "activo";
    if (request.users().user().empty() || !request.users().has_user()) {  // empty or it has no parameter
        auto *users = new chat::ConnectedUsersResponse();
        auto currentTime = std::chrono::high_resolution_clock::now();
        for (auto &i : servingCLients) {
            auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(currentTime - i.second->lastActivityTime);
            if (elapsedTime.count() >= 5) {
                // Cambiar el estado del cliente a "inactivo"
                i.second->status = "inactivo";
            }
            auto *user_in_pos = users->add_connectedusers();
            user_in_pos->set_username(i.second->username);
            user_in_pos->set_status(i.second->status);
            user_in_pos->set_ip(i.second->ip);
        }

        chat::ServerResponse response;
        response.set_servermessage("info of all connected clients");
        response.set_allocated_connectedusers(users);  // La respuesta se encarga de liberar 'users'
        response.set_option(2);
        response.set_code(200);

        string msgServer;
        response.SerializeToString(&msgServer);
        char buffer[8192];
        strcpy(buffer, msgServer.c_str());
        send(socket, buffer, msgServer.size() + 1, 0);
        std::cout << "User:" << client.username << " requested all connected";
    } else {
        ErrorResponse(2, socket, "ERROR: can't specify a user when asking all");
        std::cout << "E: User:" << client.username << " requested all with wrong args";
    }
}

void handleChangeStatus(int socket, const chat::ClientPetition& request, Cli& client) {
    servingCLients[client.username]->lastActivityTime = std::chrono::high_resolution_clock::now();
    servingCLients[client.username]->status = "activo";

    if (servingCLients.find(request.change().username()) != servingCLients.end()) {
        // Actualizar el estado del usuario
        servingCLients[request.change().username()]->status = request.change().status();
        std::cout << "User: " << client.username << " status has changed successfully\n";

        // Crear la respuesta
        chat::ChangeStatus *sStatus = new chat::ChangeStatus();
        sStatus->set_username(request.change().username());
        sStatus->set_status(request.change().status());

        chat::ServerResponse response;
        response.set_allocated_change(sStatus); // La respuesta se encarga de liberar 'sStatus'
        response.set_servermessage("status changed");
        response.set_code(200);
        response.set_option(3);

        string msgServer;
        response.SerializeToString(&msgServer);
        char buffer[8192];
        strcpy(buffer, msgServer.c_str());
        send(socket, buffer, msgServer.size() + 1, 0);
    } else {
        ErrorResponse(3, socket, "User:" + request.change().username() + " doesn't exist");
    }
}

void handleMessageSending(int socket, const chat::ClientPetition& request, Cli& client) {
    servingCLients[client.username]->lastActivityTime = std::chrono::high_resolution_clock::now();
    servingCLients[client.username]->status = "activo";

    if (!request.messagecommunication().has_recipient() || request.messagecommunication().recipient() == "everyone") { // Chat global
        std::cout << "\n__SENDING GENERAL MESSAGE__\nUser: " << request.messagecommunication().sender() << " is trying to send a general message";
        for (auto& i : servingCLients) {
            chat::MessageCommunication message;
            if (i.first == request.messagecommunication().sender()) {
                message.set_sender("you:");
                message.set_message(request.messagecommunication().message());
            } else {
                message.set_sender(client.username);
                message.set_message(request.messagecommunication().message());
            }
            chat::ServerResponse response;
            response.set_allocated_messagecommunication(new chat::MessageCommunication(message));
            response.set_servermessage("Message sent to general chat");
            response.set_code(200);
            response.set_option(4);

            string msgServer;
            response.SerializeToString(&msgServer);
            char buffer[8192];
            strcpy(buffer, msgServer.c_str());
            send(i.second->socket, buffer, msgServer.size() + 1, 0);
        }
        std::cout << "\nSUCCESS: General message sent by " << request.messagecommunication().sender() << "\n";
    } else { // Mensaje directo
        std::cout << "\n__SENDING PRIVATE MESSAGE__\nUser: " << request.messagecommunication().sender() << " is trying to send a private message to -> " << request.messagecommunication().recipient();
        auto recipient = servingCLients.find(request.messagecommunication().recipient());
        if (recipient != servingCLients.end()) {
            chat::MessageCommunication message;
            message.set_sender(client.username);
            message.set_recipient(request.messagecommunication().recipient());
            message.set_message(request.messagecommunication().message());

            chat::ServerResponse response;
            response.set_allocated_messagecommunication(new chat::MessageCommunication(message));
            response.set_servermessage("Private message sent");
            response.set_code(200);
            response.set_option(4);

            string msgServer;
            response.SerializeToString(&msgServer);
            char buffer[8192];
            strcpy(buffer, msgServer.c_str());
            send(recipient->second->socket, buffer, msgServer.size() + 1, 0);
            std::cout << "\nSUCCESS: Private message sent by " << request.messagecommunication().sender() << " to " << request.messagecommunication().recipient() << "\n";
        } else {
            ErrorResponse(4, socket, "ERROR: recipient doesn't exist");
            std::cout << "E: " + request.messagecommunication().sender() + " tried to send to a non-existing user: " + request.messagecommunication().recipient() << std::endl;
        }
    }
}

void handleUserSpecificQuery(int socket, const chat::ClientPetition& request, Cli& client) {
    servingCLients[client.username]->lastActivityTime = std::chrono::high_resolution_clock::now();
    servingCLients[client.username]->status = "activo";

    if (servingCLients.find(request.users().user()) != servingCLients.end()) {
        // Obtener el valor con la llave (username)
        auto currentUser = servingCLients[request.users().user()];
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(currentTime - currentUser->lastActivityTime);

        if (elapsedTime.count() >= 5) {
            // Cambiar el estado del cliente a "inactivo"
            currentUser->status = "inactivo";
        }

        cout << "Delta time: " << elapsedTime.count() << " user: " << request.users().user() << endl;

        chat::UserInfo userI;
        userI.set_username(currentUser->username);
        userI.set_ip(currentUser->ip);
        userI.set_status(currentUser->status);

        chat::ServerResponse response;
        response.set_allocated_userinforesponse(new chat::UserInfo(userI));
        response.set_servermessage("SUCCESS: userinfo of " + request.users().user());
        response.set_code(200);
        response.set_option(5);

        string msgServer;
        response.SerializeToString(&msgServer);
        char buffer[8192];
        strcpy(buffer, msgServer.c_str());
        send(socket, buffer, msgServer.size() + 1, 0);
        std::cout << "\n__USER INFO SOLICITUDE__\nUser: " << client.username << " requested info of ->" << request.users().user() << "\nSUCCESS: userinfo of " << request.users.user() << std::endl;
    } else {
        // El usuario no existe
        ErrorResponse(5, socket, "ERROR: user doesn't exist");
        std::cout << "\n__USER INFO SOLICITUDE__\nUser: " << client.username << " requested info of ->" << request.users().user() << "\nERROR: userinfo of " << request.users().user() << std::endl;
    }
}

void *requestsHandler(void *params) {
    struct Cli client;
    struct Cli *newClient = (struct Cli *)params; 
    int socket = newClient->socket; 
    char buffer[8192];

    // Server Structs
    string msgServer;
    chat::ClientPetition *request = new chat::ClientPetition();
    chat::ServerResponse *response = new chat::ServerResponse();
    while (1) {
        response->Clear(); // Limpiar la response enviada
        int bytes_received = recv(socket, buffer, 8192, 0);
        if (bytes_received <= 0) {
            servingCLients.erase(client.username);
            cout << "User: " << client.username << " lost connection or logged out, removed from session" << endl;
            break;
        }
        if (!request->ParseFromString(buffer)) {
            cout << "Failed to parse" << endl;
            break;
        } else {
            cout << "Option received:" << request->option() << endl;
        }
        switch (request->option()) {
            case 1:
                handleUserRegistration(socket, *request, client, *newClient);
                break;
            case 2:
                handleUserQuery(socket, *request, client);
                break;
            case 3:
                handleChangeStatus(socket, *request, client);
                break;
            case 4:
                handleMessageSending(socket, *request, client);
                break;
            case 5:
                handleUserSpecificQuery(socket, *request, client);
                break;
            default:
                // Manejar opción no reconocida o mantener vacío
                break;
        }
    }
    delete request; // Liberar memoria del request
    delete response; // Liberar memoria de la respuesta
    return nullptr;
}

int main(int argc, char const* argv[]){
    //verificar versiión de protobuf para evitar errores
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if (argc != 2){
        fprintf(stderr, "NO PORT DECLARED: server <port>\n");
        return 1;
    }
    long port = strtol(argv[1], NULL, 10);
    sockaddr_in server, incomminig_req;
    socklen_t new_req_size;
    int socket_fd, new_req_ip;
    char incomminig_req_addr[INET_ADDRSTRLEN];
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    memset(server.sin_zero, 0, sizeof server.sin_zero);

    // si hubo error al crear el socket para el cliente
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        fprintf(stderr, "ERROR: create socket\n");
        return 1;
    }

    // si hubo error al crear el socket para el cliente y enlazar ip
    if (bind(socket_fd, (struct sockaddr *)&server, sizeof(server)) == -1){
        close(socket_fd);
        fprintf(stderr, "ERROR: bind IP to socket.\n");
        return 2;
    }
	
    // si hubo error al crear el socket para esperar respuestas
    if (listen(socket_fd, 5) == -1){
        close(socket_fd);
        fprintf(stderr, "ERROR: listen socket\n");
        return 3;
    }


    // si no hubo errores se puede proceder con el listen del server
    printf("SUCCESS: listening on port-> %ld\n", port);
	
    while (1){
	    
        // la funcion accept nos permite ver si se reciben o envian mensajes
        new_req_size = sizeof incomminig_req;
        new_req_ip = accept(socket_fd, (struct sockaddr *)&incomminig_req, &new_req_size);
	    
        // si hubo error al crear el socket para el cliente
        if (new_req_ip == -1){
            perror("ERROR: accept socket incomming connection\n");
            continue;
        }
        
        
	    
        //si falla el socket, un hilo se encargará del manejo de las requests del user
        struct Cli newClient;
        newClient.socket = new_req_ip;
        inet_ntop(AF_INET, &(incomminig_req.sin_addr), newClient.ip, INET_ADDRSTRLEN);
        pthread_t thread_id;
        pthread_attr_t attrs;
        pthread_attr_init(&attrs);
        pthread_create(&thread_id, &attrs, requestsHandler, (void *)&newClient);
    }
	
    // si hubo error al crear el socket para el cliente
    google::protobuf::ShutdownProtobufLibrary();
	return 0;

}