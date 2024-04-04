#include <chrono>
#include <iostream>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocol.pb.h"


using namespace std;

struct Client{
    int socket;
    string username;
    char ip[INET_ADDRSTRLEN];
    string status;
    chrono::time_point<chrono::high_resolution_clock> latest_activity;
};

unordered_map<string,Client*> connected_clients;

void ErrorResponse(int selected_option , int socket_id , string error_description){
    char msg_buffer[8192];
    chat::ServerResponse *error_response = new chat::ServerResponse();
    string serialized_message;
    error_response->set_option(selected_option);
    error_response->set_code(500);
    error_response->set_servermessage(error_description);
    error_response->SerializeToString(&serialized_message);
    strcpy(msg_buffer, serialized_message.c_str());
    if(!send(socket_id, msg_buffer, serialized_message.size() + 1, 0)){cout<<"Fallo en el controlador de errores"<<endl;};
}

void handleUserRegistration(int socket, const chat::ClientPetition& request, Client& client, Client& new_client) {
    cout << "----------" << "DATA ENTRANTE" << "----------" << endl;
    cout << "Usuario: " << request.registration().username() << "\t IP: " << request.registration().ip();
    if (connected_clients.count(request.registration().username()) > 0) {
        cout << endl << "FALLO: usuario ya existe" << endl;
        ErrorResponse(1, socket, "ERROR: El usuario ya existe en el servidor");
        return;
    }

    // Crear la respuesta del servidor
    chat::ServerResponse response;
    response.set_option(1);
    response.set_servermessage("EXITO: Registro exitoso");
    response.set_code(200);

    // Serializar y enviar la respuesta
    string server_message;
    response.SerializeToString(&server_message);
    char buffer[8192];
    strcpy(buffer, server_message.c_str());
    send(socket, buffer, server_message.size() + 1, 0);

    cout << endl << "EXITO: El usuario " << request.registration().username() << " se agrego al sistema con el socket: " << socket << endl;

    // Actualizar la información del cliente
    client.username = request.registration().username();
    client.socket = socket;
    client.status = "activo";
    strcpy(client.ip, new_client.ip);
    connected_clients[client.username] = &client;
    connected_clients[client.username]->latest_activity = chrono::high_resolution_clock::now();
    connected_clients[client.username]->status = "activo";
}

void handleUserQuery(int socket, const chat::ClientPetition& request, Client& client) {
    connected_clients[client.username]->latest_activity = chrono::high_resolution_clock::now();
    connected_clients[client.username]->status = "activo";
    if (request.users().user().empty() || !request.users().has_user()) {  // empty or it has no parameter
        auto *users = new chat::ConnectedUsersResponse();
        auto curr_time = chrono::high_resolution_clock::now();
        for (auto &i : connected_clients) {
            auto duration = chrono::duration_cast<chrono::seconds>(curr_time - i.second->latest_activity);
            if (duration.count() >= 5) {
                // Cambiar el estado del cliente a "inactivo"
                i.second->status = "inactivo";
            }
            auto *user_input = users->add_connectedusers();
            user_input->set_username(i.second->username);
            user_input->set_status(i.second->status);
            user_input->set_ip(i.second->ip);
        }

        chat::ServerResponse response;
        response.set_servermessage("Informacion de todos los usuarios conectados");
        response.set_allocated_connectedusers(users);
        response.set_option(2);
        response.set_code(200);

        string server_message;
        response.SerializeToString(&server_message);
        char buffer[8192];
        strcpy(buffer, server_message.c_str());
        send(socket, buffer, server_message.size() + 1, 0);
        cout << "Usuario:" << client.username << " solicito la informacion de todos los usuarios conectados" << endl;
    } else {
        ErrorResponse(2, socket, "ERROR: no se debe especificar un usuario si se desea un listado general");
        cout << "FALLO: El usuario " << client.username << " solicito listado de informacion general con parametros incorrectos" << endl;
    }
}

void handleChangeStatus(int socket, const chat::ClientPetition& request, Client& client) {
    connected_clients[client.username]->latest_activity = chrono::high_resolution_clock::now();
    connected_clients[client.username]->status = "activo";

    if (connected_clients.find(request.change().username()) != connected_clients.end()) {
        // Actualizar el estado del usuario
        connected_clients[request.change().username()]->status = request.change().status();
        cout << "Usuario: " << client.username << " cambio su status" << endl;

        // Crear la respuesta
        chat::ChangeStatus *user_status = new chat::ChangeStatus();
        user_status->set_username(request.change().username());
        user_status->set_status(request.change().status());

        chat::ServerResponse response;
        response.set_allocated_change(user_status); // La respuesta se encarga de liberar 'user_status'
        response.set_servermessage("status actualizado");
        response.set_code(200);
        response.set_option(3);

        string server_message;
        response.SerializeToString(&server_message);
        char buffer[8192];
        strcpy(buffer, server_message.c_str());
        send(socket, buffer, server_message.size() + 1, 0);
    } else {
        ErrorResponse(3, socket, "Usuario:" + request.change().username() + " no existe");
    }
}

void handleMessageSending(int socket, const chat::ClientPetition& request, Client& client) {
    connected_clients[client.username]->latest_activity = chrono::high_resolution_clock::now();
    connected_clients[client.username]->status = "activo";

    if (!request.messagecommunication().has_recipient() || request.messagecommunication().recipient() == "everyone") { // Chat global
        cout << "----------"<< "ENVIANDO MENSAJE GRUPAL" << "----------" << endl;
        cout <<"Usuario: " request.messagecommunication().sender() << " esta enviando un mensaje grupal" << endl;
        for (auto& i : connected_clients) {
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
            response.set_servermessage("Mensaje enviado al chat grupal");
            response.set_code(200);
            response.set_option(4);

            string server_message;
            response.SerializeToString(&server_message);
            char buffer[8192];
            memcpy(buffer, server_message.data(), server_message.size());
            buffer[server_message.size()] = '\0';
            send(i.second->socket, buffer, server_message.size(), 0);
        }
        cout << "EXITO: Mensaje grupal enviado por: " << request.messagecommunication().sender() << endl;
    } else {
        cout << "----------" << "ENVIANDO MENSAJE DIRECTO" << "----------" << endl;
        cout << "Usuario: " << request.messagecommunication().sender() << " esta enviando mensaje directo a: " << request.messagecommunication().recipient();
        auto recipient = connected_clients.find(request.messagecommunication().recipient());
        if (recipient != connected_clients.end()) {
            chat::MessageCommunication message;
            message.set_sender(client.username);
            message.set_recipient(request.messagecommunication().recipient());
            message.set_message(request.messagecommunication().message());

            chat::ServerResponse response;
            response.set_allocated_messagecommunication(new chat::MessageCommunication(message));
            response.set_servermessage("Mensaje directo enviado");
            response.set_code(200);
            response.set_option(4);

            string server_message;
            response.SerializeToString(&server_message);
            char buffer[8192];
            memcpy(buffer, server_message.data(), server_message.size());
            buffer[server_message.size()] = '\0';
            send(recipient->second->socket, buffer, server_message.size(), 0);
            cout << "EXITO: Mensaje directo enviado por: " << request.messagecommunication().sender() << " a: " << request.messagecommunication().recipient() << endl;
        } else {
            ErrorResponse(4, socket, "ERROR: El destinatario no existe");
            cout << "FALLO: " + request.messagecommunication().sender() + " intento enviar mensaje directo a usuario inexistente: " + request.messagecommunication().recipient() << endl;
        }
    }
}


void handleUserSpecificQuery(int socket, const chat::ClientPetition& request, Client& client) {
    connected_clients[client.username]->latest_activity = chrono::high_resolution_clock::now();
    connected_clients[client.username]->status = "activo";

    if (connected_clients.find(request.users().user()) != connected_clients.end()) {
        // Obtener el valor con la llave (username)
        auto current_user = connected_clients[request.users().user()];
        auto curr_time = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::seconds>(curr_time - current_user->latest_activity);

        if (duration.count() >= 5) {
            // Cambiar el estado del cliente a "inactivo"
            current_user->status = "inactivo";
        }

        cout << "Cambio de tiempo: " << duration.count() << " Usuario: " << request.users().user() << endl;

        chat::UserInfo user_data;
        user_data.set_username(current_user->username);
        user_data.set_ip(current_user->ip);
        user_data.set_status(current_user->status);

        chat::ServerResponse response;
        response.set_allocated_userinforesponse(new chat::UserInfo(user_data));
        response.set_servermessage("EXITO: informacion del usuario: " + request.users().user());
        response.set_code(200);
        response.set_option(5);

        string server_message;
        response.SerializeToString(&server_message);
        char buffer[8192];
        strcpy(buffer, server_message.c_str());
        send(socket, buffer, server_message.size() + 1, 0);
        cout << "----------" << "SOLICITUD DE INFORMACION DE USUARIO" << "----------" << endl; 
        cout << "Usuario: " << client.username << " solicito informacion del usuario: " << request.users().user() << endl;
        cout << "EXITO: informacion de usuario: " << request.users().user() << endl;
    } else {
        ErrorResponse(5, socket, "ERROR: El usuario solicitado no existe");
        cout << "FALLO: solicitud de informacion de usuario inexistente: " << request.users().user() << endl;
    }
}

void *requestsHandler(void *args) {
    struct Client client;
    struct Client *new_client = (struct Client *)args; 
    int socket = new_client->socket; 
    char buffer[8192];

    // Server Structs
    string server_message;
    chat::ClientPetition *request = new chat::ClientPetition();
    chat::ServerResponse *response = new chat::ServerResponse();
    while (1) {
        response->Clear(); // Limpiar la response enviada
        int bytes_received = recv(socket, buffer, 8192, 0);
        if (bytes_received <= 0) {
            connected_clients.erase(client.username);
            cout << "Usuario: " << client.username << " cerro sesion, eliminado del servidor" << endl;
            break;
        }
        if (!request->ParseFromString(buffer)) {
            cout << "FALLO: No se pudo interpretar el string" << endl;
            break;
        } else {
            cout << "Opcion seleccionada:" << request->option() << endl;
        }
        switch (request->option()) {
            case 1:
                handleUserRegistration(socket, *request, client, *new_client);
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
        cout << "ERROR: No se declaro un puerto" << endl;
        return 1;
    }
    long port = strtol(argv[1], NULL, 10);
    sockaddr_in server, incoming_request;
    socklen_t request_size;
    int socket_desc, request_ip;
    char request_address[INET_ADDRSTRLEN];
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    memset(server.sin_zero, 0, sizeof server.sin_zero);

    // si hubo error al crear el socket para el cliente
    if ((socket_desc = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        cout << "ERROR: No se pudo crear el socket" << endl;
        return 1;
    }

    // si hubo error al crear el socket para el cliente y enlazar ip
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) == -1){
        close(socket_desc);
        cout << "ERROR: No se pudo enlazar la IP al Socket" << endl;
        return 2;
    }
	
    // si hubo error al crear el socket para esperar respuestas
    if (listen(socket_desc, 5) == -1){
        close(socket_desc);
        cout << "ERROR: No se pudo recibir solicitud del socket" << endl;
        return 3;
    }


    // si no hubo errores se puede proceder con el listen del server
    cout << "EXITO: Esperando solicitudes en el puerto: " << port << endl;
	
    while (1){
	    
        // la funcion accept nos permite ver si se reciben o envian mensajes
        request_size = sizeof incoming_request;
        request_ip = accept(socket_desc, (struct sockaddr *)&incoming_request, &request_size);
	    
        // si hubo error al crear el socket para el cliente
        if (request_ip == -1){
            cout << "ERROR: No se pudo aceptar la conexion entrante al socket" << endl;
            continue;
        }
        
	    
        //si falla el socket, un hilo se encargará del manejo de las requests del user
        struct Client new_client;
        new_client.socket = request_ip;
        inet_ntop(AF_INET, &(incoming_request.sin_addr), new_client.ip, INET_ADDRSTRLEN);
        pthread_t thread_id;
        pthread_attr_t attrs;
        pthread_attr_init(&attrs);
        pthread_create(&thread_id, &attrs, requestsHandler, (void *)&new_client);
    }
	
    // si hubo error al crear el socket para el cliente
    google::protobuf::ShutdownProtobufLibrary();
	return 0;

}
